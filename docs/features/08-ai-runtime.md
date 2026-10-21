# Features: AI runtime (AIR)

Owner: `plugins/chat/code` (the chat plugin's server code, a built-in on the plugin API, `docs/26`) unless noted. Design: `docs/04` (models, routing, privacy, context, MCP, egress), `docs/12` (agent loop, reliability), `docs/13` (tools), `docs/14` (system prompt).

## Providers

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-1 | OpenAI-compatible Chat Completions provider (llama.cpp server, vLLM, SGLang, LM Studio, Ollama `/v1`, OpenRouter, any compatible), streaming SSE, tool calling, vision content parts, custom headers | P0 | US-MDL-1 |
| F-AIR-2 | Endpoint discovery: `GET /v1/models`; llama.cpp `/props` (real context size, slot count, chat template), Ollama `/api/show` (num_ctx); prefer discovered values over typed ones and warn on mismatch | P0 | US-MDL-6 |
| F-AIR-3 | Capability probe ("Test connection"): tiny tool-call round trip, vision probe, measure TTFT and tokens/sec; result stored with the model profile | P0 | US-MDL-6 |
| F-AIR-4 | Dialect adapters per model profile: reasoning field (`reasoning_content` / `reasoning` / `thinking` / `<think>` tags in content), tool-call encoding (native / Hermes `<tool_call>` XML / Qwen / Mistral `[TOOL_CALLS]` / harmony), parallel-call delta merging by id then index | P0 | US-MDL-7 |
| F-AIR-5 | Anthropic Messages API provider (API key): streaming, thinking, tool use, prompt caching, rate-limit headers -> usage widget | P1 | US-CLD-3 |
| F-AIR-6 | Claude Code bridge provider: drives the user's own installed `claude` CLI in stream-json mode with IED's tools exposed via MCP; reports `rate_limit_event` usage (see `docs/04`, policy constraints) | P1 | US-CLD-1, US-CLD-2 |
| F-AIR-7 | Embeddings provider (OpenAI-compatible `/v1/embeddings`, llama.cpp) for skill/history search | P2 | US-CTX-4 |
| F-AIR-8 | Plugin-contributed providers (gen) - a plugin can add a provider type | P2 | US-PLG-5 |
| F-AIR-9 | Model profile fields: endpoint, model id, display name, context window, max output, supports tools/vision/thinking/FIM, effort mapping (how "low/med/high" maps to this model: `reasoning_effort`, thinking budget, or a system hint), default sampling (temperature, top_p, top_k, min_p, repeat penalty), parallel slots, privacy class (`local` / `lan` / `cloud`), cost per token, stall policy overrides | P0 | US-MDL-1 |

## Model groups and routing

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-10 | Model groups: role -> { model, temperature, effort, fallback list }; roles: `primary`, `planning`, `implementation`, `research`, `worker`, `summarizer`, `titler`, `vision`, `completion` (FIM), `inline` | P0 | US-MDL-2 |
| F-AIR-11 | Group tiers: app (defined), solution (override which group is default, or override roles), chat (selected) | P0 | US-MDL-2 |
| F-AIR-12 | Scheduler: per-model concurrency = discovered/configured slots; requests queue per model with priority (interactive > agent > background like titles/summaries) | P0 | US-MDL-4 |
| F-AIR-13 | Fallback on failure classes: rate-limited (429 / usage exhausted), unreachable, context too small, capability missing; next eligible model in the role's fallback list; visible routing note | P1 | US-MDL-3 |
| F-AIR-14 | Load-aware choice among equivalents ("class" tags on models; a busy primary can yield to an idle model of the same class if the group allows it) | P1 | US-MDL-4 |
| F-AIR-15 | Capability-aware routing: images -> vision-capable; long context -> model whose window fits; FIM -> completion role | P1 | US-MDL-8 |
| F-AIR-16 | Never-route rules: privacy (below) always wins over availability; a private request queues for a local model rather than fall back to cloud | P0 | US-MDL-5 |

## Privacy

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-20 | Privacy classes on models; chat-level `private` flag (local + lan only) | P0 | US-MDL-5 |
| F-AIR-21 | Solution policy `aiEgress.models: any | local-only | lan-only` in `.ied/` (committed: a repo can declare itself private) | P0 | US-MDL-5 |
| F-AIR-22 | Taint tracking: path rules (`.ied/privacy.json` globs, e.g. `secrets/**`, `**/.env`, `personal/**`) mark content sensitive; once a chat's context contains sensitive content, the chat becomes local-only (sticky, visible lock reason); clearing requires forking without the content | P1 | US-MDL-5 |
| F-AIR-23 | Optional content detectors (API-key patterns, private keys) that taint and warn; never block silently | P2 | US-MDL-5 |
| F-AIR-24 | Privacy also gates tools: in a private chat, web tools follow the egress policy (tor by default if configured) and MCP servers flagged `cloud` are hidden | P1 | US-MDL-5, US-WEB-2 |

## Agent loop and reliability (design: `docs/12`)

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-30 | Agent loop: request -> stream -> tool calls (parallel when declared safe) -> results -> next request, until a terminal condition; all state persisted per step | P0 | US-AGT-3 |
| F-AIR-31 | Modes: Ask, Plan (read-only tools + ask_user + research), Agent (autonomous: completion requires an explicit `finish` call with a verification summary), custom modes from `.ied/modes/*.json` (instructions, tool allowlist, path globs, model group, budgets) | P0 | US-AGT-3 |
| F-AIR-32 | Stall watchdog: separate timers for (a) time-to-first-byte while prompt processing, extended by provider progress signals when available, (b) inter-chunk idle; both per-model configurable; on expiry: cancel + retry policy | P0 | US-AGT-1 |
| F-AIR-33 | Premature-stop detection: `finish_reason=length` -> auto-continue; empty response; reasoning-only response; text that announces an action but has no tool call; unparsed tool-call text; unknown finish reasons treated as "ended" (not fatal) -> bounded nudge/continue policy | P0 | US-AGT-1 |
| F-AIR-34 | Transport resilience: retry with backoff on connect errors/5xx/429; mid-stream disconnect keeps partial output and continues (prefix-continuation where supported, else regenerate the step); malformed chunks skipped and logged, not fatal | P0 | US-AGT-1 |
| F-AIR-35 | Tool-call repair: truncated JSON repair, schema validation with an error result the model can fix, unknown tool -> list of close names, duplicate call ids de-duplicated | P0 | US-AGT-1 |
| F-AIR-36 | Restart safety: runs interrupted by a server restart resume automatically (setting: auto / ask / never) from the last persisted step | P1 | US-AGT-4 |
| F-AIR-37 | Loop and thrash detection: repeated identical failing calls, oscillating edits, no-progress windows -> pause with explanation | P1 | US-AGT-5 |
| F-AIR-38 | Budgets per run/chat/mode: tokens, wall time, tool calls, cost; soft warning at 80%, pause at 100% | P1 | US-AGT-6 |
| F-AIR-39 | Sub-agents: `spawn_agent(role, task, tools?)` with role models/temperatures, parallelism bounded by model slots, private transcripts, bounded reports, cancellation; the parent can wait on several | P1 | US-AGT-2 |
| F-AIR-40 | Hooks: pre/post tool, post-edit, pre-request, turn-end - user-configured commands or plugin bindings; can veto with a message | P2 | US-AGT-7 |
| F-AIR-41 | Template hygiene: no mid-history `system` messages (steering is sent as user-role notes), no `name` fields unless the dialect supports them; strict alternation repair for templates that require it | P0 | US-AGT-1 |

## Context

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-50 | Token accounting: provider-reported usage is authoritative; a tokenizer per model family (or llama.cpp `/tokenize`) for estimates; never rely on chars/3.5 for budget decisions on local models | P0 | US-CTX-1 |
| F-AIR-51 | Context ledger: every item typed and counted (system parts, messages, tool results, attachments, skills, summaries) - feeds the context panel | P0 | US-CTX-1 |
| F-AIR-52 | Tiered compression: (1) mechanical - supersede old file reads, trim tool results to conclusions + excerpts, drop stale diagnostics; (2) summarize older segments with the `summarizer` role into a structured summary (goal, constraints, decisions, progress, files, open questions, exact snippets to keep) | P0 | US-CTX-1 |
| F-AIR-53 | Cache-aware compression: append-only between compressions; compress in large steps at a high-water mark so llama.cpp/Anthropic prefix caches stay valid; keep the system prompt + early stable prefix byte-identical | P1 | US-CTX-7 |
| F-AIR-54 | Raw history never lost: `history_search` / `history_read` tools let the agent retrieve compressed-away details | P0 | US-CTX-1 |
| F-AIR-55 | Overflow recovery: provider context-exceeded error -> parse the real limit, update the model profile, compress, retry (bounded) | P0 | US-CTX-1 |
| F-AIR-56 | Manual compress now (`/compact` with optional instructions), and "pin" items so they are never compressed | P1 | US-CTX-1 |

## System message and knowledge

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-60 | System message assembly in fixed order: base prompt (`docs/14`) -> user style rules (app tier) -> solution `AGENTS.md` -> plugin sections (bounded each) -> workspace summary -> mode instructions -> skills index (bounded) | P0 | US-CTX-2, US-CTX-3 |
| F-AIR-61 | Nested `AGENTS.md`: when the agent first reads/edits a file under a folder with its own `AGENTS.md` (or a federation member root), that file is injected once as a context note | P1 | US-CTX-5 |
| F-AIR-62 | Skills: sources (project `.ied/skills/`, app-tier, plugin-bundled); frontmatter `name`, `description`, `keywords`, `triggers` (C4 skills use `triggers`); index with BM25 over name/description/keywords/triggers/headings; optional embedding rerank | P0 (keyword), P2 (embeddings) | US-CTX-4 |
| F-AIR-63 | Skills index in the prompt: if <= N skills, list name + one-liner; above N, list only top-K relevant to the current task and rely on `skill_search` | P0 | US-CTX-4 |
| F-AIR-64 | Trajectory store per chat (JSONL) with rotation and retention limits (size and age), raw media stored as files not base64 | P0 | US-CHT-17 |
| F-AIR-65 | Skill authoring: `skill_write` tool (approval-gated) and "Synthesize skill from this chat" action | P1 | US-CTX-6 |
| F-AIR-66 | Project memory (`.ied/memory/`): small fact files the agent can read/append with approval; reviewable list in settings | P1 | US-CTX-8 |

## MCP

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-70 | MCP client: stdio and streamable HTTP transports; tools, resources (as @-mentionable context), prompts (as slash commands) | P1 | US-TOOL-6 |
| F-AIR-71 | MCP server config at app / solution / project tier (project-defined servers need trust); per-server enable, env, privacy class, egress policy | P1 | US-TOOL-6 |
| F-AIR-72 | MCP server supervision: start on demand, restart on crash with backoff, logs to an output source, health in settings | P1 | US-OPS-1 |
| F-AIR-73 | IED as an MCP server (stdio shim + HTTP): exposes a curated set of IED tools (LSP navigation, build/test, editor open, diagnostics) to external agents; per-client allowlist | P1 | US-TOOL-9 |
| F-AIR-74 | Remote MCP OAuth flows | P2 | - |

## Web egress

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-80 | Egress proxy in the host: every AI-originated HTTP request (fetch, search backends, browser tool, MCP HTTP) goes through it; policy per workspace: `direct | tor | proxy:<profile>`; untrusted workspaces: off | P0 (direct + off), P1 (tor/proxy) | US-WEB-2 |
| F-AIR-81 | Bundled SearXNG managed by the host (start on demand, health, engine config in settings, upstream via the egress proxy) | P0 | US-WEB-1 |
| F-AIR-82 | Bundled tor (managed process, SOCKS port, bootstrap status, new-identity on rate limit) | P1 | US-WEB-2 |
| F-AIR-83 | VPN profiles = SOCKS5/HTTP proxy endpoints (a provider's proxy, or a `gluetun` container started via the sandbox plugin for WireGuard/OpenVPN providers) | P1 | US-WEB-2 |
| F-AIR-84 | `web_fetch` converts HTML to clean markdown (readability-style), handles PDFs to text, caps size, and summarizes big pages with the `summarizer` role when asked | P0 | US-WEB-3 |
| F-AIR-85 | Rate-limit awareness: per-domain backoff, circuit breaker for engines that start returning captchas, rotate tor circuit | P1 | US-WEB-2 |

## Cloud usage (Claude)

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-AIR-90 | Usage tracker per cloud account: Claude subscription utilization (5-hour and 7-day windows, resets) from the bridge's rate-limit events / status; API-key remaining tokens and requests from response headers; cost estimate per chat | P1 | US-CLD-2 |
| F-AIR-91 | Usage view in the chat bar (F-CHT-31) and in settings (history chart of utilization) | P1 | US-CLD-2 |
| F-AIR-92 | Limit-aware routing input: utilization >= threshold marks the model "soon exhausted" so long background work prefers other models | P2 | US-MDL-3 |
