# 04 — AI chat design

## Purpose

The AI is a first-class citizen of IED, not an add-on. This doc covers the runtime that the chat plugin (05) sits on, plus the cross-cutting pieces: model groups, context compression, system-message assembly, skills search, MCP, and web egress.

## Where things live

- **AI runtime (host, `src/ai/`):** model connections, streaming, token accounting, tool-call execution, context management, egress proxying. Not a plugin — it's infrastructure, like the LSP pool.
- **Chat plugin (`plugins/chat/`):** all UI (tabs, streaming views, context panel) *and* the default tool-call set (file/shell/web tools). The plan explicitly says these default tools belong to the chat plugin.

## Model configuration & model groups

Settings define **models** (endpoint URL, API style — OpenAI-compatible is the baseline — auth, context window size, tool-calling support, vision support) and **model groups**: named bundles that pick a model per *role*:

```jsonc
{
  "groups": {
    "default":  { "primary": "local-big",   "researchSubAgent": "local-fast", "workerSubAgent": "local-fast" },
    "fast":     { "primary": "local-fast" }
  }
}
```

- The chat UI shows a **model group selector** per chat tab (plus per-message overrides later).
- Roles exist so sub-agent work (research, parallel workers) can run on cheaper/faster models while the main loop uses the strong one. Group membership is editable in settings; the runtime resolves role → model at call time.
- Per-chat and per-message **effort** setting (maps to reasoning effort / thinking budget where the model supports it).

## Streaming & telemetry

- Everything streams: thinking blocks, normal messages, tool calls — rendered in real time as they arrive.
- Each segment carries metadata shown in the UI: duration, tokens, tokens/sec. Tool calls show success/failure + elapsed time.
- The chat bar shows aggregate session stats (total tokens, current speed).
- **Collapsible right-side breakdown** per session: context usage (what's consuming the window), list of modified files, tool-call log summary.

## Context management — the long-chat problem

Goal: "fast and high quality context compression so we can have long running chats." Working design:

1. **Structured context ledger.** Every item in the conversation is tracked with type (user / assistant / thinking / tool-call / tool-result / injected), token count, and an importance class. Tool results are the big ones — they're summarizable by construction (they have structure).
2. **Tiered compression**, triggered when usage crosses thresholds (e.g. 70% warn, 85% compress):
   - *Lossless first:* truncate/summarize large tool results to their conclusions + key excerpts; drop superseded file reads (a later read of the same file replaces the earlier one).
   - *Summarizing second:* a cheap model group member summarizes older conversation segments into compact narrative entries that preserve decisions, open questions, and file paths.
   - The ledger stays authoritative: the UI's context panel always shows what's live vs compressed, and compression is visible to the user (never silent).
3. **Skills are never in-context by default** — they're searched on demand (below), which is what makes "a massive amount of skills" viable.

## System message assembly

Built per session from, in order:

1. Base IED identity + UI conventions.
2. **`AGENTS.md`** from the solution root, if present (read verbatim; this repo's own AGENTS.md is the template for what it should contain).
3. **Plugin `systemMessageSections`** — small appended notes from enabled plugins (e.g. C4 language plugin: "Docs for this project are in docs/…"). Bounded size per section so one plugin can't blow the context; overflow gets truncated with a pointer to where the full text lives.
4. Project/solution summary (solution name, projects, build system) — generated from `.ied.sln` / `.ied.proj`.

## Skills search

- Skills = markdown files with frontmatter (`name`, `description`, `keywords`). Sources: `.ied/skills/` (project), global app-data skills, plugin-bundled skills.
- At session start the AI gets **only metadata** (name + one-line description) for all skills — cheap even at hundreds of skills.
- A `searchSkills(keywords)` tool call returns full skill bodies on demand; the host does keyword + fuzzy matching over name/description/keywords fields (full-text index later if needed).
- This mirrors how the C4 repo already ships skills (`Nuke/skills/<name>/SKILL.md`), so C4's existing skill content ports straight into `.ied/skills/`.

## Tool calls — default set (chat plugin)

| Tool | Notes |
|---|---|
| `fs.read / fs.write / fs.search` | workspace-scoped; writes tracked for the "modified files" panel; @-mention aware |
| `shell.exec` | runs via the terminal supervisor; streaming output; timeout + permission-gated |
| `loadImage / loadVideo` | if the model supports vision; from workspace paths or attachments |
| `web.search` | routed through packaged SearXNG (below) |
| `web.fetch` | same egress path |
| `renderPage(url)` / `screenshot()` | headless render to get console output + DOM; interact-with-page actions (click/type/extract) for web work |
| `chat.inject(text)` | user-injected guidance mid-run ("inject commands into the chat") |
| `searchSkills(keywords)` | see above |

Plugins add tools on top: editor exposes build/run/debug/LSP-navigation tools; sandbox exposes "run this in linux container"; profiler exposes session queries. All tool calls are permission-checked and appear live in the UI with status + timing.

## MCP support

- Host runs **MCP client** connections to external servers (blender, Claymore tooling, anything). Servers configured in settings (command or URL), per app / solution / project tier.
- MCP tools surface into the same tool-call registry as native ones — the model doesn't care where a tool came from; the UI shows the source.
- MCP servers launched for a workspace inherit that workspace's egress policy (below).

## Web egress & privacy ("secure mode")

- **SearXNG is packaged with IED** and runs as a local process managed by the host — no separate setup, per the plan. Configurable (engines, instance list) in settings.
- All AI web traffic (search + fetch + MCP egress) goes through the host's **egress proxy**, which can route through:
  - direct (default), or
  - **tor / a user-configured VPN** when the project's "secure mode" is enabled — prevents rate limiting and adds privacy.
- Egress policy is per-solution (`.ied/solution.json`): `direct | tor | vpn:<profile>`. Untrusted workspaces: egress off until trusted.

## Attachments & @-mentions

- Drag-drop or picker to attach files; images/videos go to vision-capable models, text files get path-referenced and read on demand.
- Typing `@` in the chat input opens a file-path suggestion list (fuzzy, workspace-scoped); inserting produces a structured reference the model resolves to real content at call time — not pasted blindly into context.

## Open items

- Compression trigger thresholds + which role does summarization (default: group's `workerSubAgent` on a fast local model).
- SearXNG packaging details: bundled binary vs vendored docker image; tor integration via system tor vs bundled. Decide in design phase.
- Whether sub-agents (research/worker) get their own visible chat tabs or stay inside the parent tool-call view. Leaning: collapsed sections inside the parent, with an option to expand into a full tab.
