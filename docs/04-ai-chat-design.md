# 04 - AI design: models, routing, privacy, context, knowledge, egress

## Purpose

The AI is a first-class citizen of IED. This doc covers the AI runtime's cross-cutting design. Companion docs: `docs/12` (agent loop, modes, sub-agents, stall protection), `docs/13` (tools), `docs/14` (system prompt), `docs/features/07-chat.md` + `08-ai-runtime.md` + `16-orchestration.md` (feature lists).

## Where things live

- **AI runtime (`src/ai/`, host infrastructure, not a plugin):** providers, dialect adapters, scheduler and routing, agent loop, tool executor, permissions, context ledger and compression, persistence, egress proxy, MCP client, usage tracking, goal queue and verifier.
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

Groups are defined at app tier; a solution can pick its default group or override roles; each chat selects a group (and effort). This is the plan's "Claude plans, a temp-0 local model implements" made concrete (journey J3).

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
  1. *Mechanical:* superseded file reads dropped (a later read of the same file wins), large tool results trimmed to conclusions + excerpts, stale diagnostics dropped.
  2. *Summary:* older turns summarized by the `summarizer` role into a structured summary (goal, constraints, decisions, progress, files touched, open questions, exact snippets to keep).
- **Cache-aware:** the system message and early prefix stay byte-identical; between compressions the context is append-only; compression happens in big steps rarely, so llama.cpp and Anthropic prefix caches stay warm (this is what makes long local chats *fast*, not just possible).
- **Never lost:** the raw history stays on disk; `history_search` / `history_read` retrieve compressed-away details. Scratchpad and task list are never compressed (they travel as tail notes).
- **Overflow recovery:** a context-length error updates the model's window from the error text, compresses and retries (`docs/12`).

## System message

Assembled in a fixed order - base prompt, style rules, mode instructions, solution `AGENTS.md`, bounded plugin sections, workspace summary, tool-group index, skills index - with volatile state kept out of it. Exact rules and the shipped texts: `docs/14`, `src/ai/prompts/`. Nested `AGENTS.md` files (subfolders, federation members) are injected as tail notes the first time the agent works in that area.

## Skills

- Sources: `.ied/skills/` (project, committed), `%APPDATA%/IED/skills/`, plugin `skills` paths. Format: `shared/host-api/README.md` (compatible with the C4 repo's existing skills, which use `triggers`).
- Index: BM25 over name, description, keywords, triggers and headings; optional embedding rerank (P2).
- In the prompt: the whole index when small (<= 40), otherwise the top 15 for the task + `skill_search`. Bodies load only via `skill_read`; a skill can declare tool groups to enable.
- Authoring: `skill_write` (approval-gated) and "synthesize skill from this chat"; the orchestration learning loop proposes skill updates after successful goals.

## MCP

MCP client for stdio and streamable-HTTP servers (Blender, Claymore, anything), configured at app/solution/project tier (project-defined servers need trust). Tools surface as group `mcp_<server>` in the same registry; resources become @-mentionable context; prompts become slash commands. Each server has a privacy class and inherits the workspace egress policy. IED is also an MCP *server* (P1) so external agents can use its LSP/build/editor tools.

## Web egress and secure mode

- **SearXNG bundled** and managed by the host (`docs/21`); `web_search` goes through it.
- **Egress proxy:** all AI-originated HTTP (fetch, search upstreams, browser tool, MCP HTTP) goes through the host's proxy with a per-workspace policy `direct | tor | proxy:<profile>`; untrusted workspaces have egress off.
- **tor** bundled (managed process, new circuit on rate limiting); **VPN profiles** are SOCKS5/HTTP proxy endpoints (a provider's proxy, or a `gluetun` container started via the sandbox plugin for WireGuard/OpenVPN providers).

## Persistence

Per chat, under `.ied/local/chats/<chatId>/` (generic chats under `%APPDATA%/IED/chats/`):

| File | Content |
|---|---|
| `chat.json` | header: title, mode, group, permissions, privacy/taint state, task list, scratchpad, pinned items |
| `turns.jsonl` | completed turns and segments (the transcript) - append-only, compacted occasionally |
| `steps.jsonl` | run journal (state transitions, tool call started/finished) for crash-safe resume |
| `trajectory/*.jsonl` | raw requests/responses/timings, rotated with retention limits |
| `media/` | attachments and tool media as files (never base64 in JSONL) |
| `checkpoints/` | content-addressed pre-edit file snapshots for rewind |

Only the active window of each chat stays in server memory; older turns load lazily on scroll.

## Attachments and @-mentions

Drag-drop, paste and picker attachments (images/videos to vision-capable models; others become file references read on demand). `@` opens fuzzy suggestions: files and folders, symbols (`@#Name`), open documents, terminals, output sources, Error List, `@diff`, other chats; mentions become structured references resolved at call time.
