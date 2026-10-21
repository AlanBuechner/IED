# 05 - Default plugins

The seven plugins that ship with IED. They are ordinary plugins (`docs/02`) of kind `builtin` - compiled into the server because C4 has no DLLs - using the same manifest, contribution points and bindings as anyone else; nothing about them is special-cased in the host, and each can be disabled. Full feature lists live in `docs/features/`; this doc gives each plugin's role, what it owns, and how it composes with the others.

| Plugin | Sidebar | Owns | Feature file |
|---|---|---|---|
| `ied.chat` | 1st (top) | chat UI, Mission Control, approvals, scratchpad, tasks, default AI tool bindings | `features/07-chat.md`, `features/16-orchestration.md` |
| `ied.editor` | 2nd | Monaco, Solution Explorer, document well, toolbar, Output, Error List, Find, Test Explorer, debugger windows, viewers, inline AI | `features/04-editor.md`, `05-build-run-test.md`, `06-debugger.md` |
| `ied.git` | 3rd | Fork/GitKraken-class source control | `features/09-git.md` |
| `ied.terminal` | none (bottom dock) | always-present terminal, AI terminals | `features/10-terminal.md` |
| `ied.sandbox` | plugin order | Docker/WSL/SSH sandboxes, sandbox terminals, AI sandbox tools | `features/11-sandbox.md` |
| `ied.profiler` | plugin order | capture library, timeline, analysis views, AI profiler tools | `features/12-profiler.md`, `docs/16` |
| `ied.gpu` | plugin order (after profiler) | GPU capture library, frame debugger views, replay backends (separate native processes), insights, compare, AI GPU tools | `features/17-gpu-debugger.md`, `docs/22` |

Host modules they sit on (not plugins): `src/fs` (documents, search), `src/tasks`, `src/terminal` (PTYs), `src/lsp`, `src/dap`, `src/git`, `src/ai`, `src/sandbox`, `src/profiler`, `src/spell`.

## chat

The first sidebar icon. Browser-like chat tabs, dockable side by side; streaming thinking/text/tool calls with per-segment timing and tokens; tool cards with status and elapsed time; stop reasons and harness notes always visible; chat bar with model group, effort, mode, permission mode, privacy lock, token totals, tokens/s and context fill; approval and question cards answerable from any device; collapsible right panel (context usage, modified files, tool stats, tasks, scratchpad, sub-agents, run info); scratchpad and task list; history search, fork, rewind; generic (no-project) chats. **Mission Control** (all runs across workspaces + the attention queue) and the goal board are part of this plugin.

Bindings provided: `chat.inject`, `chat.openTab`, `chat.attach { chatId, items }`, `chat.getActiveContext`, `orc.createGoal`. Consumes: `editor.openFile`, `editor.showDiff`, `terminal.run`.

**As built (M4, first slice):** `plugins/chat` declares the `ai.*` settings (`docs/04` "As built") and contributes the Chat activity (first in the sidebar and the phone's bottom bar), the status bar's "AI: 1 running, 1 needs you" and `ied.chat.show` (Ctrl+Alt+I), `ied.chat.new` (Ctrl+Alt+N), `ied.chat.stop`. The activity: a tab per chat of the workspace (chips on a phone; a tab closed on one device stays a chat, in All chats), the transcript (thinking folded, Markdown replies, tool cards that open to their result or diff, approval cards with Allow once / Allow for this chat / Always allow / Deny with message and Alt+A / Alt+D, the ask_user card, harness notes, each turn's stop, recoveries, tokens and speed), the composer (Enter sends or queues, Ctrl+Enter injects, Esc stops; group, mode and permission; tokens, speed, time to first token, context use) and the right panel (context by part, tasks, modified files that open in the editor). Not yet: effort and the privacy lock, @-mentions and attachments, the scratchpad, sub-agents, Mission Control.

## editor

The Visual Studio heart of IED: Monaco on server-owned buffers; LSP features through profiles contributed by language plugins; Solution Explorer; preview tabs and splits including bound header/source splits; map-mode scrollbar; track-changes margin; find/replace in file, project, solution, federation; Output window with source dropdown and ANSI colors; Error List merging build, LSP and spell diagnostics; toolbar rendered from project tasks; Test Explorer; VS debugger windows over the host DAP pool; markdown preview with interactive checkboxes; image/video/hex and plugin viewers; browser pane; code-aware spell check UI; inline AI (Ctrl+I) and AI actions on menus and lightbulbs; ghost-text completion.

Bindings provided (largest surface in IED): `editor.openFile`, `editor.getSelection`, `editor.applyEdit`, `editor.showDiff`, `editor.revealProblem`, `tasks.run` (UI side), `editor.getOutput`; plus AI tools for symbol navigation that wrap LSP requests (`docs/13`).

## git

Sidebar icon under the editor. Commit graph, branches/remotes/tags/stashes/submodules/worktrees, commit details with diffs, file history and blame, staging by file/hunk/line, commit composer with AI-drafted messages, fetch/pull/push, merge/rebase (interactive) /cherry-pick/revert/reset, 3-way conflict resolution with optional AI help, LFS. Backend: the **git CLI** (`docs/99` #14). Worktrees also power agent isolation (`features/16-orchestration.md`).

## terminal

Not a sidebar tab: an always-present, collapsible shell docked at the bottom across all activities, full width minus the sidebar. ConPTY sessions that outlive the browser with server-side scrollback; profiles (PowerShell, pwsh, cmd, Git Bash, WSL, sandbox shells); multiple instances and splits; links and shell integration. The same supervisor runs task processes and the AI's shell commands - each chat gets a visible "AI: <chat>" terminal, so the owner sees exactly what the AI ran.

**As built (M3):** `plugins/terminal` contributes the `panels` view (`docs/02`) over the server's terminals (`docs/10` "Terminals", `src/services/terminals`): a tab per terminal of the workspace (`terms:<wsId>`, so every window lists the same; double-click renames in place; a context menu renames or kills), New (Ctrl+Shift+`), Kill, show/hide (Ctrl+`), Open in Terminal on a folder in Solution Explorer; xterm.js in the theme's ANSI colors, the screen rebuilt from the server's scrollback; a window that does not hold the lease watches, with Type Here. `terminal.shell` picks the program (default PowerShell 7, Windows PowerShell, cmd). On a phone the panel is a slide-up drawer from More with Esc, Tab, Ctrl+C and the arrows. Not yet: splits, profiles beyond the setting, links, shell integration, the AI's terminals (M4).

## sandbox

Providers: Docker (Linux images), WSL, SSH (a Mac for macOS work, an ARM Linux box, any build machine) - Docker cannot legitimately run macOS on PC hardware, so "other OS" is generalized. Per-project sandbox definitions in `.ied/sandbox/`, solution bind-mounted, deny-by-default egress with grants, approved setup scripts, task targets (`target: sandbox:<name>`), container-to-host path mapping for errors, AI tools to run/build/test inside autonomously. Degrades with clear guidance when Docker is absent. For C4 projects it becomes fully useful once C4 targets Linux (`docs/17`, P-7).

## profiler

Tracy-class profiler built on the ITP trace protocol (`docs/16`). Tracy itself is a **feature reference only** - never linked into IED or C4. Timeline with frames, zones, plots, messages, locks; **thread renames as a time series with tinted segments and a by-task view**; statistics, find-zone, flame graph, compare; memory (live allocations, leaks by call stack, pools, GC events); GPU lanes with calibration (D3D12 first); capture library; Chrome-trace import (Nuke build traces, game-engine profiles); AI query tools. Instrumentation comes from Bedrock `Profile::` (`C4/Bedrock/`) so C4 programs are profileable by default.

## gpu

PIX/RenderDoc/Nsight-class graphics debugger (`docs/22`). Capture library with build and commit metadata; Event Browser with GPU timing columns; Pipeline State with bindless resolution; Texture Viewer with browser-side display transforms and backend overlays; Mesh Viewer; Resources; Pixel History; Shader Debugger in an editor surface with Edit and Replay; Performance (timing, queue timeline, counter providers, insights); capture compare. Replay runs in a separate native backend process per capture (`rdc` over RenderDoc's C++ replay API first; IED's own Flashbang later), so a driver crash never takes the server down and no Python is involved.

Bindings provided: `gpu.captureFrame`, `gpu.openEvent`, `gpu.attachToChat`. Consumes: `editor.openFile`, `editor.applyEdit`, `tasks.run`, `profiler.openZone`, `chat.attach`.

## Language plugins

Declarative plugins (`docs/02`) that ship with IED and bring languages through the host's generic pools, so nothing about a language lives in the host:

- `ied.languages`: ids, extensions, comment tokens and brackets for common languages, and C/C++ header/source pairs, so every document has a language (Monaco's tokenizers color them) before a dedicated plugin adds more.
- `ied.cpp` (C/C++): clangd as the language server of `c` and `cpp` documents (one per workspace; `cpp.clangd.path`, default `clangd` from `PATH`, and `cpp.clangd.arguments`; `cpp.languageServer` turns it off). clangd finds each file's flags in the nearest `compile_commands.json` (or `compile_flags.txt`) above it or in a `build/` folder beside one - what CMake and most build systems write - and guesses without one. Debugging through LLDB with CodeLLDB's adapter (`cpp.lldb.path`: the vscode-lldb extension's `adapter/codelldb.exe`, or `codelldb` on `PATH`), adapter type `lldb` in a project's `debug` configurations (`docs/25`); it debugs Rust too.
- `ied.rust` (Rust): rust-analyzer as the language server of `rust` documents (`rust.analyzer.path`, default `rust-analyzer` from `PATH` - rustup's, once its `rust-analyzer` component is installed); the `rust.cargo` task preset for a folder with a `Cargo.toml` (a Debug / Release profile; Build, Run, Test and Clean running `cargo`, `rust.cargo.path`) with the `rustc` problem matcher over cargo's `--message-format=short`; and a `Launch` configuration that debugs `target/<profile>/<project>.exe` through `ied.cpp`'s CodeLLDB after the build. Checked live: rust-analyzer running, a type error at its line in the Error List, a breakpoint stopping with its locals.
- One plugin per further language as its server is wired (Python, Go, the web languages): a profile that runs the language's usual server, its path a setting.

A language server that is not installed shows as failed in `lsp.status` with the reason ("no program named "clangd" was found") and the language keeps its basic support.

## Cross-plugin notes

- Every plugin exposes its useful capabilities as **skills** (how to use them) and, where actionable, **AI tools** and **bindings**.
- Default sidebar order: workspace switcher, chat, editor, git, then plugin activities by `order`; notifications and settings at the bottom.
- Minimal configuration: IED must remain usable with only `ied.editor` and `ied.terminal` enabled (documented and tested).
