# 04 - AI design: models, routing, privacy, context, knowledge, egress

## Purpose

The AI is a first-class citizen of IED. This doc covers the AI runtime's cross-cutting design. Companion docs: `docs/12` (agent loop, modes, sub-agents, stall protection), `docs/13` (tools), `docs/14` (system prompt), `docs/features/07-chat.md` + `08-ai-runtime.md` + `16-orchestration.md` (feature lists).

## Where things live

- **AI runtime (the `ied.chat` plugin's server code, `plugins/chat/code/`, a built-in on the plugin API - `docs/26` "Built-ins on the API"):** providers, dialect adapters, scheduler and routing, agent loop, tool executor, permissions, context ledger and compression, persistence, egress proxy, MCP client, usage tracking, goal queue and verifier.
- **Chat plugin (`plugins/chat/`):** all chat UI (tabs, streaming views, approval cards, right panel, scratchpad, tasks, Mission Control) and the default tool set's bindings.

## Providers

| Provider | Covers | Notes |
|---|---|---|
| OpenAI-compatible Chat Completions | llama.cpp server, vLLM, SGLang, LM Studio, Ollama `/v1`, OpenRouter, others | the baseline; streaming SSE, tools, vision parts |
| Anthropic Messages API | Claude with an API key | thinking, tool use, prompt caching, rate-limit headers |
| Claude Code bridge | Claude through the owner's own installed `claude` CLI | see "Claude" below |
| Plugin providers | anything else | `modelProviders` contribution |

**Model profiles** (app tier) hold: endpoint, model id, context window (discovered), max output, capabilities (tools, vision, thinking, FIM), effort mapping, default sampling, parallel slots (discovered from llama.cpp `/props` where available), privacy class (`local` / `lan` / `cloud`), cost, prompt variant, dialect, stall-policy overrides. Discovery and a capability probe fill most of it ("Test connection").

**Dialects** normalize what local servers actually emit: reasoning fields (`reasoning_content`, `reasoning`, `thinking`, `<think>` tags), tool calls as text (Hermes, Qwen, Mistral, harmony, fenced JSON), parallel call deltas (merge by id then index), finish reasons. Details and the reasons they matter: `docs/12`.

## Model groups and roles

A **model group** assigns, per **role**, a model + temperature + effort + fallback list:

```jsonc
"ai.groups": {
	"plan-claude/impl-local": {
		"primary":        { "model": "qwen3.8-27b@spark", "temperature": 0.3, "effort": "medium", "fallback": ["qwen-coder-32b@desk"] },
		"planning":       { "model": "claude-opus-5-5",   "temperature": 0.3, "effort": "high",   "fallback": ["qwen3.8-27b@spark"] },
		"implementation": { "model": "qwen-coder-32b@desk","temperature": 0.0 },
		"worker":         { "model": "qwen-coder-32b@desk","temperature": 0.0 },
		"research":       { "model": "qwen3.8-27b@spark", "temperature": 0.2 },
		"verifier":       { "model": "qwen3.8-27b@spark", "temperature": 0.0 },
		"summarizer":     { "model": "qwen-small@desk",   "temperature": 0.1 },
		"titler":         { "model": "qwen-small@desk",   "temperature": 0.2 },
		"vision":         { "model": "qwen-vl@spark" },
		"completion":     { "model": "qwen-coder-fim@desk" },
		"inline":         { "model": "qwen-coder-32b@desk", "temperature": 0.1 }
	}
}
```

Groups are defined at app tier; a solution can pick its default group or override roles; each chat selects a group (and effort). A role a group leaves unset is **off** and uses the group's `primary` (a group without `primary` uses the first model in `ai.models`); a new group turns on `primary` alone, so the finer roles stay off until the owner gives one its own model. This is the plan's "Claude plans, a temp-0 local model implements" made concrete (journey J3).

## Scheduling and routing

1. **Role -> candidate list** (the role's model, then its fallbacks).
2. **Filter**: privacy (a private/tainted chat or local-only solution removes `cloud`, and `lan` if the policy says so), capabilities (vision, tools, FIM), context fit (prompt + reserve <= discovered window), availability (not marked exhausted or unreachable).
3. **Pick**: first candidate with a free slot; if none is free, queue on the first candidate (interactive requests ahead of agent work ahead of background work like titles/summaries); optional scorecard preference among equivalents (`docs/features/16`).
4. **On failure** (429/usage exhausted, unreachable, context too small): mark the model's state, move to the next candidate, show a routing note in the chat.

Privacy always wins: a private request waits for a local slot rather than fall back to the cloud.

## Privacy

- **Classes** on models and MCP servers (`local`, `lan`, `cloud`).
- **Per-chat private toggle** (lock icon) and **solution policy** `aiEgress.models: any | local-only | lan-only` in `.ied/solution.json` (committed: a repo can declare itself private).
- **Taint tracking:** `.ied/privacy.json` path globs (and optional detectors for key-like strings) mark content sensitive. When sensitive content enters a chat's context (file read, attachment, tool result), the chat becomes local-only and stays so (sticky), with the reason shown on the lock. Removing taint means forking without that content.
- Private chats also restrict tools: web tools follow the egress policy (tor if configured), `cloud` MCP servers are hidden.

## Claude

IED is primarily local-AI; Claude is optional. Anthropic's policy (Agent SDK overview and legal docs, checked 2026-10-19) says third-party products may not offer claude.ai login or route requests through Free/Pro/Max credentials, and may not collect or intermediate claude.ai credentials; an end user may run the unmodified Claude Code CLI signed in themselves. So:

1. **Anthropic API provider** (API key in the Windows credential store): clean for any use. Usage view from `anthropic-ratelimit-*` response headers + cost estimates.
2. **Claude Code bridge** (personal use): IED spawns the owner's own installed `claude` CLI headless (`-p --input-format stream-json --output-format stream-json --verbose --include-partial-messages`), removes its built-in tools (`--tools ""`), exposes IED's tools to it through an MCP server config (`--mcp-config ... --strict-mcp-config`), routes its permission prompts to an IED approval tool (`--permission-prompt-tool`), and maps its stream (assistant/tool/result/`rate_limit_event`) onto IED's chat events. IED never reads, stores or asks for Claude credentials; the owner signs in to the CLI themselves. Subscription usage (5-hour and 7-day utilization, reset times) comes from the CLI's `rate_limit_event` stream and status data. Whether this path is enabled for anyone other than the owner is open question O2.
3. Model ids current at writing: `claude-opus-5-5`, `claude-sonnet-5-5`, `claude-haiku-5-5`, `claude-fable-5-1` (verify at implementation time).

Flags and event shapes must be re-verified against the installed CLI version when implementing (they change across releases).

## Context management

- **Ledger:** every context item typed and counted (provider usage is authoritative; per-family tokenizers or llama.cpp `/tokenize` for estimates). Feeds the right-panel context view.
- **Tiered compression** at a high-water mark (defaults: warn 70%, compress at 85% of the discovered window, target 50%):
  1. *Mechanical:* superseded file reads dropped (a later read of the same file wins), large tool results trimmed to conclusions + excerpts, stale diagnostics dropped. As built: at a compression a read the same range of the file was read again after becomes "[superseded: <path> was read again later]", and one the file was written or edited after becomes "[out of date: ...; read it again if you need it]" (`plugins/chat/code/superseded.cf`); then older results are trimmed and whole earlier turns left out.
  2. *Summary:* older turns summarized by the `summarizer` role into a structured summary (goal, constraints, decisions, progress, files touched, open questions, exact snippets to keep).
- **Cache-aware:** the system message and early prefix stay byte-identical; between compressions the context is append-only; compression happens in big steps rarely, so llama.cpp and Anthropic prefix caches stay warm (this is what makes long local chats *fast*, not just possible).
- **Never lost:** the raw history stays on disk; `history_search` / `history_read` retrieve compressed-away details. Scratchpad and task list are never compressed (they travel as tail notes).
- **Overflow recovery:** a context-length error updates the model's window from the error text, compresses and retries (`docs/12`).

## System message

Assembled in a fixed order - base prompt, style rules, mode instructions, solution `AGENTS.md`, bounded plugin sections, workspace summary, tool-group index, skills index - with volatile state kept out of it. Exact rules and the shipped texts: `docs/14`, `plugins/chat/prompts/`. Nested `AGENTS.md` files (subfolders, federation members) are injected as tail notes the first time the agent works in that area.

## Skills

- Sources: `.ied/skills/` (project, committed), `%APPDATA%/IED/skills/`, plugin `skills` paths. Format: `shared/host-api/README.md` (compatible with the C4 skills the toolchain's plugin ships, `C4/IedPlugin/skills/`, which use `triggers`).
- Index: BM25 over name, description, keywords, triggers and headings; optional embedding rerank (P2).
- In the prompt: the whole index when small (<= 40), otherwise the top 15 for the task + `skill_search`. Bodies load only via `skill_read`; a skill can declare tool groups to enable.
- Authoring: `skill_write` (approval-gated) and "synthesize skill from this chat"; the orchestration learning loop proposes skill updates after successful goals.

## MCP

MCP client for stdio and streamable-HTTP servers (Blender, a game engine, anything), configured at app/solution/project tier (project-defined servers need trust). Tools surface as group `mcp_<server>` in the same registry; resources become @-mentionable context; prompts become slash commands. Each server has a privacy class and inherits the workspace egress policy. IED is also an MCP *server* (P1) so external agents can use its LSP/build/editor tools.

## Web egress and secure mode

- **SearXNG bundled** (`docs/21`); `web_search` goes through it. As built (`docs/29`): run by the `ied.websearch` plugin, not the host, its engines asked through Tor (the `ied.tor` plugin) - Tor carries web search only for now (owner, 2026-10-21); `web_fetch` (`ied.webfetch`) goes direct, looking like a browser. The egress proxy below is not built.
- **Egress proxy:** all AI-originated HTTP (fetch, search upstreams, browser tool, MCP HTTP) goes through the host's proxy with a per-workspace policy `direct | tor | proxy:<profile>`; untrusted workspaces have egress off.
- **tor** bundled (managed process, new circuit on rate limiting); **VPN profiles** are SOCKS5/HTTP proxy endpoints (a provider's proxy, or a `gluetun` container started via the sandbox plugin for WireGuard/OpenVPN providers).

## Persistence

Per chat, under `.ied/local/chats/<chatId>/` (generic chats under `%APPDATA%/IED/chats/`):

| File | Content |
|---|---|
| `chat.json` | header: title, mode, group, permissions, privacy/taint state, task list, scratchpad, pinned items, the clipboard |
| `turns.jsonl` | completed turns and segments (the transcript) - append-only, compacted occasionally |
| `steps.jsonl` | run journal (state transitions, tool call started/finished) for crash-safe resume |
| `trajectory/*.jsonl` | raw requests/responses/timings, rotated with retention limits |
| `media/` | attachments and tool media as files (never base64 in JSONL) |
| `checkpoints/` | content-addressed pre-edit file snapshots for rewind |

Only the active window of each chat stays in server memory; older turns load lazily on scroll.

## Chats talk to each other (built, 2026-10-21)

A chat can hand work to another chat of its workspace and hear back, the way a lead hands a task to a colleague. The tools are the deferred group `chats` (`docs/13`), loaded with `tool_search` or by calling one:

- `chat_list` names the workspace's chats (id, title, mode, running or idle, the first line of its last answer); `chat_read` shows another chat's last turns (at most 5), messages and answers without the tool calls.
- `chat_send` sends a chat a message: an idle chat starts a turn with it, a running one gets it at its next step (like a steering message). `chat_create` starts a new chat with a message (a title and mode optional; it uses this chat's model group), to split the work. Both are Exec tools: they ask in Ask mode, and "Allow for this chat" adds `chat.send(<id>)` / `chat.create(*)`.
- `chat_wait` waits for another chat's turn to end and returns its answer (default 120 s, at most 600 s; a stop ends the wait).

The receiving turn says who sent it: its user message carries `from {chatId, title}`, the transcript heads it "From chat <title>" with a link that opens that chat, and the model is told the message came from that chat and that it can answer with `chat_send`. A chain of chats messaging chats is cut at 3 hops (`hops` on the turn), so two chats cannot keep each other running forever.

## What a chat has open (built, 2026-10-21)

Other plugins open things on a chat's behalf - the Browser plugin's pages an AI drives (`docs/27`), and later terminals and processes. A plugin says so through the chat plugin's server binding `chat.resource` (`{chatId, ref, title?, kind?, open?}`; `open: false` takes it off): `ref` is the tab that shows it (`browser:pg-3`). The list is what is open now, kept for the server run only (after a restart nothing is loaded, and a plugin reports a tab again when it loads it); the chat's topic says when it changes (`resources.changed`) and the side panel lists it under "Open by this chat", each row opening its tab, so the owner can watch the AI work in the page.

## The AI's clipboard (built, 2026-10-21)

A chat copies text it has seen - lines of a file, of an earlier tool result, or text it gives - to a named clip with `clipboard_copy`, and pastes it into any later call by writing `{{clip:<name>}}` in a string of the call's arguments: a long block matched exactly as `fs_edit`'s `old`, a function moved to another file, an error message reused in a search, without the model typing it again (`docs/13` `clipboard`). The clips are the chat's own, kept in `chat.json`, and the side panel lists them under "Clipboard" (name, where it came from, its lines), each unfolding to its text, so the owner sees what a call pasted.

## Attachments and @-mentions

Drag-drop, paste and picker attachments (images/videos to vision-capable models; others become file references read on demand). `@` opens fuzzy suggestions: files and folders, symbols (`@#Name`), open documents, terminals, output sources, Error List, `@diff`, other chats; mentions become structured references resolved at call time.

**Design (2026-10-22), built.** What the owner puts in a message besides its words, and how the model gets it:

- **Mentions.** Typing `@` at a word's start in the composer (or its `@` button) opens a list above the text box that narrows
  as more is typed; Up/Down choose, Enter or Tab inserts, Esc closes. The list is the client's, from methods it already has: files
  (`fs.files`, open documents first) and their folders (`@src/`), symbols for `@#` (`lsp.workspaceSymbols`), terminals
  (`term.list`), output sources (`out.sources`), the workspace's other chats, and two fixed entries, `@problems` (the
  Error List) and `@diff` (the working tree's changes). Picking one writes `@<label>` into the text - `@src/ring.cf`,
  `@#Game::Ring::Push`, `@terminal:Build`, `@output:Git`, `@chat:Fix the build` - and the composer keeps it as a
  structured reference, `{kind, ref, label}` (`kind` one of `file`, `folder`, `symbol`, `terminal`, `output`,
  `problems`, `diff`, `chat`; `ref` the path, the symbol's place `path:line` (1-based, its label's name narrowing
  it; a bare name works too), the terminal or source id, or the chat id). A mention whose `@<label>` is no
  longer in the text when it is sent is dropped.
- **Attachments.** The paper clip (a file picker), a paste of files or an image, or files dropped on the composer. Each
  is uploaded at once (`PUT /api/upload`, `docs/10`) and shows as a chip above the text box - an image's picture, any
  other file's icon, name and size - with a button that takes it off. A workspace file dragged from the Solution
  Explorer is a mention of it, not an attachment.
- **The message** goes as `chat.send {..., mentions: [{kind, ref, label}], attachments: [{blobId, name}]}` (`docs/10`).
  The server copies each attachment out of its blob into the chat's `media/` folder (`.ied/local/chats/<chatId>/media/
  m<n>-<name>`, `n` the chat's counter and the name's last part with anything but letters, digits, `.`, `-` and `_`
  made `_`; at most 16 per message and 20 MiB each; an expired blob is refused with `not_found`), and the
  turn's user message keeps `attachments: [{name, path, mime, bytes}]` (the path workspace-relative) and `mentions`.
- **Resolved when the turn starts** - "at call time": the text the model gets for each mention is read when the turn's
  first request is built, not when it was typed, and appended to the user's message as one block per mention,
  `<mention label="@src/ring.cf" kind="file">...</mention>`. Most are what the read tool for them answers, so the model
  sees a mention as it would see that tool's result (`docs/13`):
  - a file: `fs_read` - the text the document store has (an open document's unsaved edits included), its first 400
    lines, and how to read on;
  - a folder: `fs_list`;
  - a symbol: `code_read_symbol`, its declaration with its doc comment;
  - `@problems`: `problems_list`, the workspace's Error List;
  - an output source: its last 200 lines (the new `IED::OutputTail`);
  - a terminal: its last 200 lines without escape sequences, from the terminal plugin's binding `terminal.text`
    (plugins compose through bindings, `AGENTS.md` rule 7);
  - `@diff`: each of the workspace's repositories' `git diff HEAD` and the files it does not track (IED's
    `.ied/local/` left out), at most 64 KiB, from the git plugin's binding `git.workingDiff`;
  - a chat: `chat_read` of its last 3 turns.
  A mention that cannot be read (a file deleted since, a chat gone) is a block that says why. The resolved message is
  the turn's stored user message, so a retry, a restart and the transcript show what the model saw; later steps of the
  turn do not read the mentions again.
- **Attachments in the request:** an image (PNG, JPEG, GIF, WebP, at most 4 MiB) goes to a model that takes images as an
  image part of the owner's own message (its text first), counted with a tool result's images among the newest 4 a
  request sends (`docs/13` "Images in tool results"); to a model that
  does not, and for any other file, the message names it - `attached: .ied/local/chats/<id>/media/2-notes.txt (12 KB,
  text/plain)` - and the model reads it with `fs_read` when it needs it.
- **The transcript** shows a user message's mentions and attachments as chips under its text; an image chip shows the
  picture (the workspace's images route, `docs/10`), a file chip opens the file, a mention chip opens what it names.

New in the plugin API for it (`docs/26`): `IED::ReadBlob(id, contentType, bytes, problem)` (an upload's bytes) and
`IED::OutputTail(wsId, sourceId, text, problem)` (the text an output source keeps); new bindings: the terminal
plugin's `terminal.text` and the git plugin's `git.workingDiff`, which the chat's manifest consumes.

**As built.** Server: `plugins/chat/code/sent` (chat.send's mentions checked, attachments copied), `mentions` (each
mention read at turn start, `TurnRun::ResolveMessage`), `images` (`WriteUserWithImages`); a queued message keeps its
mentions and attachments in `chat.json`; `plugins/terminal/code/terminaltext`, `plugins/git/code/workingdiff`. Client:
`plugins/chat/ui/mentions.js` (the rows: `fs.files`, `lsp.workspaceSymbols` after `@#`, `term.list`, `out.sources`,
`chat.list`), `mentionlist.js` (the list), `attachbar.js` (uploads and their chips), `userchips.js` (the transcript's
chips). Tests: `server.ai.mentions`, `server.terminal.text`, `server.git.workingdiff`, `tools/server-checks/tests/
test_chat_mentions.py`, `frontend/tests/chatmentions.test.js`, the e2e scenario `chat_mentions_and_attachments`. A
queued message keeps what it names and carries through the queue row's Edit (`chat.unqueue` answers it whole; its files
go again by their media path) and Send it now (`chat.unqueue {start: true}` starts it on the server).

## As built (M4, first slice, 2026-10-21)

The runtime is the `ied.chat` plugin's server code (`plugins/chat/code/`, compiled into the server and written against `src/api/` like any plugin's, since P4 of `docs/26`; before, `src/ai/`); the plugin's UI module is its UI and its manifest declares its settings and the AI output source:

| Part | Files | Does |
|---|---|---|
| Wire | `wire/sse`, `wire/request`, `wire/completion`, `wire/dialect` | SSE events and comments; streaming requests (a POST, or a GET) over Bedrock `Net::`/`Http::`; a completion put together from chunks (reasoning fields, calls merged by id then index, usage and llama.cpp timings, errors in the stream, malformed chunks counted and skipped); `<think>` blocks split from content across chunks and calls written as text in six dialects |
| Step | `step` | one request under the watchdogs, text released with a held tail so a call's markup never shows as prose |
| Classification | `classify` | the stop taxonomy of `docs/12`, `announced_no_action` from the reply's last sentence |
| Models | `models`, `probe` | profiles (`ai.models`) and a group's role candidates (`ai.groups`): the role's model, then its fallbacks, an off role the primary; what a model's server says of itself |
| Permissions | `permissions` | `ai.permissions` rules and the chat's mode (`docs/11`) |
| Tools | `tools/` | the core toolset (`docs/13` "As built") |
| Prompts | `prompts` | the system message from `plugins/chat/prompts/` (base, style, mode, the workspace's `AGENTS.md`, a workspace summary) and the harness notes by name |
| Run | `run`, `runstream`, `runtools` | the loop of `docs/12` on a thread per turn |
| Chats | `chat`, `chatstore`, `service` | the model, persistence and `chat.*` (`docs/10` "Chats") |

**Settings** (declared by `plugins/chat/settings.schema.json`):

| Key | Tiers | Value |
|---|---|---|
| `ai.models` | app | `{<name>: {endpoint, model?, contextWindow?, maxOutput?, privacy?, vision?, apiKey?: {env}, watchdog?: {connectS, prefillBaseS, prefillRate, idleS, stepCapS}}}`: an OpenAI-compatible endpoint (`http://host:port/v1`); `vision` says whether the images tool results show are sent to it (`docs/13` "Images in tool results"; left out, its server says: llama.cpp's `/props` `modalities.vision`); a key is only ever named by its environment variable |
| `ai.groups` | app | `{<group>: {<role>: {model, temperature?, effort?, fallback?: [names]}}}`; a turn uses the role `primary` |
| `ai.defaultGroup` | app, solution | the group of a chat that names none; empty: the first group; with no group at all, the first model |
| `ai.permissions` | app | `{allow, confirm, deny}`: lists of `kind(pattern)` rules |
| `ai.maxSteps` | app, solution | model requests per turn before it pauses; 0, the default, is no limit |
| `ai.tools.loadAll` | app, solution | send every tool's definition with every request; off, the default, plugin tools and tool groups are listed by name and loaded on demand (`tool_search`, `docs/13`) |

**Persistence:** per chat under `<workspace>/.ied/local/chats/<chatId>/`: `chat.json` (title, mode, permission, group, tasks, queued messages, the rules allowed for the chat, the files the AI changed), `turns.jsonl` (each finished turn with the messages the model was sent) and `running.json` (the turn in flight, written at every step and tool boundary). A `running.json` found at start is a turn a restart interrupted: it ends `interrupted`, a call it had running gets a result saying its effect is unknown, and the chat waits paused for the next message.

**Discovery and settings:** `ai.probe` asks a model's server what it serves (`docs/10`), and llama.cpp's `/props` whether it takes images (`vision`, its `modalities`); a turn asks each profile's server once per server run: a profile without `contextWindow` uses the slot context it reports, a profile without `vision` takes what the server says of images, and a llama.cpp server streams the prompt read so far (`docs/12` "Watchdogs"). Settings > AI (`docs/07` "The settings window as built"): **Models** lists the profiles with Test connection and a form to add or change one (written to the User tier; "Images tool results show" is `vision`: as its server says - left out -, send them, or never); **Model Groups** shows the groups (the default starred; New, Duplicate, Rename, Make default, Delete) and the roles table - an on/off toggle per role, a model, a temperature, an effort and fallbacks in order. An off role is grayed out and uses the primary.

**Not yet:** the Anthropic and Claude Code providers, Ollama's `/api/show`, the scheduler with slots and privacy routing, the summary tier of compression (only the mechanical tier: old tool results trimmed, then whole earlier turns left out of the request), MCP, sub-agents, checkpoints and rewind, the trajectory files.
