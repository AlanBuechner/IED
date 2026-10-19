# 05 — Default plugins

Feature breakdown per default plugin. Each entry: **role, key features, bindings it exposes/consumes, notes for the implementer.** All of these live in `plugins/<name>/` and are ordinary plugins — nothing about them is special-cased in the host.

## chat (`plugins/chat/`)

The first sidebar icon (top). Browser-like tabs at the top of its panel, one per conversation; multiple chats can be **docked side-by-side** (no free-floating windows — docking is DOM layout).

- Real-time streaming: thinking blocks and tool calls render as they type; each segment shows duration, tokens, tokens/sec; tool calls show success/failure + elapsed.
- Chat bar: model group selector, effort selector, session token totals + current speed; **cloud-model subscription usage** (e.g. Claude: remaining in session, rollover/reset times) shown where the provider reports it.
- Collapsible right-side breakdown: context usage ledger, modified-files list, per-session info.
- Inject commands mid-conversation to steer or add work.
- **Scratchpad:** an internal planning/note-taking surface for the AI (and user) — a markdown pane that can be opened inline in the chat window or docked as a tab beside it; the model gets a `scratchpad` tool call (read/append/replace sections). Persists with the chat session.
- Chats persist to disk and survive crashes; old messages evict from memory lazily (see 04, "Chat persistence").
- Owns the **default tool-call set** (see 04) and exposes bindings: `chat.inject`, `chat.openTab`, `chat.getActiveContext`.

## editor (`plugins/editor/`) — the heart of the IDE

Must feel like Visual Studio, not VS Code. Components:

- **Monaco text editor** with full LSP support via registered LSP profiles (host manages the connections; the plugin renders). High-quality completion and navigation come from the LSP server, per the plan.
- **Code-aware spell check** — robust, language-model aware (not a naive dictionary): uses LSP symbol info so identifiers/types aren't flagged, flags natural-language-looking text in comments/strings. This is a differentiator; budget real time for it.
- **Project tree (left):** solutions → groups → projects → files, VS-style with distinct icons per file type (icon set lives in `shared/ui-kit/`).
- **Toolbar (top):** per-project dropdowns (platform / configuration / architecture / custom) + buttons that execute **command lists** with values injected from the dropdowns; each step can target a different output tab. E.g. Run = build → on success → launch app, with build log to Build output and app stdout to Run/Debug output. Buttons carry images/icons. All configurable in project files (see 06). DAP support is built into the host; buttons just configure it.
- **Output window:** VS-style — a *dropdown* selects the tab (not literal tabs); ANSI console colors preserved; build / run-debug / test outputs route to their configured tabs.
- **Search:** in-file, project-wide, solution-wide; regex toggle; find & replace with preview — VS parity.
- **Splits:** split views and **bound splits** that auto-open the matching header/source pair (`.ch` ↔ `.cf`, extension pairs configurable per LSP profile).
- **Markdown preview** with interactive elements (checkboxes in tables/lists are clickable and write back to the file) + a built-in browser for testing web pages + generic file viewer fallback.
- **Inline AI chat:** edit files in place via the chat's tool calls; edits show as pending diffs inline, accept/reject.
- **Minimap** on the scrollbar.
- **Line indicators:** green = changed & saved (vs VCS), yellow = unsaved changes — VS semantics, exactly.
- Context menus (right-click) in VS style: open, open split, copy path, reveal in tree, git actions, AI actions ("explain", "fix", "chat with selection").
- **Indentation fixer tool:** a tool call (exposed to the AI + right-click menu) that normalizes file indentation — project default is **tabs** (the system prompt tells the model to always use tabs; this tool is the mechanical backstop).
- Exposes the biggest binding surface in IED: `editor.openFile`, `editor.getSelectedText`, `editor.applyEdit`, `editor.runToolbarButton(id)`, `editor.getOutput(tabId)`, plus **LSP-wrapped navigation tools for the AI** (go-to-def, references, hover/type info, formatting via custom LSP endpoints) — so the model can navigate code by symbol without reading whole files.

## git (`plugins/git/`)

Sidebar icon under editor. A Fork/GitKraken-class tool, not a status list:

- Repo state: branches, remotes, stashes; commit graph view with per-commit file trees and diffs (diff clicks jump to editor via `editor.openFile`).
- Stage/unstage per hunk, commit message composer (AI-assisted message generation via chat binding).
- Fetch/pull/push/merge/rebase flows with conflict UI that reuses the editor's diff + inline AI resolution.
- Host provides git plumbing (`src/git/`); this plugin is pure UI + orchestration.

## terminal (`plugins/terminal/`)

**Not a sidebar tab.** An always-present, collapsible shell docked at the bottom, full width minus the sidebar (VS-style).

- Runs PowerShell (Windows) / bash; multiple terminal instances; working directory follows the active project's root.
- This is also the backend for the AI's `shell.exec` tool and for toolbar command execution — one supervisor (`src/terminal/`) serves all three, so the AI sees exactly what a human would see.
- Exposes bindings: `terminal.run`, `terminal.getActiveOutput`.

## sandbox (`plugins/sandbox/`)

Docker-based cross-OS development: run a container (linux, macOS-ish) to build/test/develop a project on another OS.

- Container lifecycle managed by host (`src/sandbox/`): image selection per project (settings), volume-mount the solution root, port forwarding for the built app.
- Toolbar integration: a "Run in Linux" button is just a toolbar button whose command template targets the sandbox — no special UI needed.
- **AI-facing:** the model gets a tool to build/test/run inside the sandbox autonomously ("verify this compiles on linux"), with streamed output back into the chat as a tool result.
- Prereq: docker installed; plugin degrades gracefully (disabled + hint) when it isn't.

## profiler (`plugins/profiler/`)

Tracy-class, fully featured: find perf issues **and memory leaks**. **Tracy (`third_party/tracy`) is a feature reference only — study its views and trace format for design; it is never linked into IED or C4, not a runtime dependency.**

- Timeline view (CPU), allocation view (leaks/lifetime), call stacks, thread view.
- **Dynamic thread renaming:** threads can change name mid-execution; the UI tints the segment where a rename happened, and offers a **"by task" grouping** so pooled worker threads are viewed by the task they're running, not by OS thread — this is a first-class view mode, not a filter.
- **GPU trace ingestion** (Claymore's GPU work shows up alongside CPU).
- Data flow: instrumented app → profiler library in C4's default lib (Bedrock) → session file/socket → plugin UI. The C4-side hooks are C4-repo work (see AGENTS.md coordination list); the plugin consumes a documented trace format — define that format early, it's the seam between repos.
- Integrates with toolbar: "Profile" button = build + launch under profiler; `build.status` binding auto-starts capture when a run launches.

## Cross-plugin notes

- Every plugin above must expose its useful capabilities as **skills** (so the AI knows how to use them) and, where actionable, as **tool calls**.
- Sidebar order is fixed by default: chat (top), editor, git — then plugin-contributed icons in manifest order.
- All six ship with IED but are individually disable-able; the app must remain usable without any of them except editor+shell (document the minimal config).
