# 20 - Predecessor IDE: parity floor, surpass targets, migration

## Purpose

The predecessor IDE (Go + TypeScript, ~640 commits) is the IDE IED replaces. Owner directive: **parity is the floor; IED must drastically surpass it.** This doc lists (a) every predecessor capability the owner relies on, mapped to the IED features that cover it (nothing may regress), (b) how far past the predecessor each area goes, (c) what IED deliberately leaves to plugins, and (d) how a predecessor user's data moves over.

Inventory source: code-level review of the predecessor IDE on 2026-10-19 (README, `docs/`, `internal/`).

## Parity floor -> IED coverage -> how IED surpasses it

| # | Predecessor capability | IED features | Surpass |
|---|---|---|---|
| 1 | Any OpenAI-compatible endpoint; per-endpoint headers, sampling, context/max tokens, effort; idle watchdog; routing (chat/research/vision/inline) | F-AIR-1..4, 9..16 | Discovered (not typed) context sizes; dialect adapters; role-based groups with temperatures; slot-aware scheduler; limit/load/capability/privacy routing; scorecards |
| 2 | Streaming reasoning shown separately; TTFT + usage metrics | F-CHT-10..13, 30 | Per-segment timing and tokens/s, live context bar, routing notes, stop reasons |
| 3 | Multi-tab persistent chats; fork from any message; edit/rerun/delete; stop; clear | F-CHT-1..5, 60..64 | Docking, checkpoints + rewind of files, queue vs inject, search across history |
| 4 | Cross-workspace chat map | F-CHT-6, F-ORC-1 | Mission Control with live state and inline approvals |
| 5 | Modes: General, Plan (questions + implement buttons), Goal (durable, steerable, restart-safe), Pair, custom modes with tool/path allowlists | F-AIR-31, F-CHT-52, agent-mode schema | Agent mode needs verified `finish`; independent verifier; auto-resume after restart; budgets; worktree isolation |
| 6 | Context compression keeping raw history + `conversation_history_search` | F-AIR-50..56 | Ledger, tiered and cache-aware compression, real tokenizers, pinned items, visible compression log |
| 7 | Recovery from `length`, empty responses, context overflow | `docs/12` | Full stop taxonomy (13 classes) with bounded recovery, text tool-call parsing, mid-stream continuation, prefill watchdog |
| 8 | Read-only research sub-agents (owner note: "not working correctly") | F-AIR-39 | Research *and* worker sub-agents, slot-bounded parallelism, nested visible transcripts; regression-tested with the fake model server |
| 9 | Trajectory with viewer, search, export | F-AIR-64, F-CHT-92/93 | Retention limits (the predecessor grew a 134 MB file), media as files, "report a stop" bundle |
| 10 | File-change summary per turn; Trash-backed deletes | F-CHT-43, F-OPS-13 | Per-turn checkpoints with rewind |
| 11 | Monaco + LSP templates, DAP debugging, tests with coverage, Ctrl+I inline chat, hot-exit buffers, external-change detection | EDT, BLD, DBG areas | VS-class: Error List, problem matchers, Test Explorer, debugger windows, toolbar task templates; buffers on the server (not browser IndexedDB); inline sessions survive reload |
| 12 | Git, Fossil, P4; PTY terminals with saved commands | GIT, TRM | Fork-class git (graph, hunks/lines, interactive rebase, LFS, worktrees); AI terminals per chat; Fossil/P4 via provider plugins (P2) |
| 13 | Workspace skills, skill synthesis from a chat | F-AIR-62..65 | BM25 + optional embeddings over thousands of skills, plugin-bundled skills, learning loop proposals |
| 14 | Media upload/lightbox; ComfyUI image generation; SearXNG | F-CHT-18/22, F-AIR-81 | Bundled SearXNG; tor/VPN egress; image compare and window capture tools. ComfyUI -> plugin (not core) |
| 15 | Owner auth, session management, mobile layout, notifications/sounds | `docs/11`, F-SRV-30..44, F-SHL-40..45 | Loopback hardening (the predecessor bound all interfaces, no Host check), device revoke, phone-first views, Web Push |
| 16 | Multi-browser safety (LSP doc lease + Take Over, revision-checked debug, file revision conflicts, desktop input lease) | `docs/11` concurrency table | Same rules extended to chat sends, approvals, terminals, git, settings; presence |
| 17 | Docker sandbox with GUI desktop and takeover, deny-by-default egress | SBX | Sandbox providers (docker/WSL/SSH incl. a Mac or ARM box), task targets, path mapping for errors |
| 18 | Plugin API with approval gating | PLG | Bindings, 40+ contribution points, declarative language plugins, project-local plugins, process + JS runtimes |
| 19 | Safe mode; self-update and rebuild-relaunch | F-PLG-34, F-OPS-25, F-OPS-27 | Rebuild-relaunch keeps clients connected (resume) |

## Predecessor weaknesses IED designs out

From the predecessor's own code and docs: harness stop causes (`docs/12` table); no AGENTS.md loading (the prompt mentions it, no code reads it); no budgets, `MaxChatRounds` unenforced; unbounded trajectories; single-file transcripts with inline base64 media; no network retry; plain HTTP on all interfaces; no per-tool approval; docs drifting from code (Kanban/Orchestrator designs never built - IED's ORC area builds them, and `AGENTS.md` makes "docs match behavior" a rule).

## Deliberately not core

| Predecessor feature | IED placement |
|---|---|
| ComfyUI image/video generation tools | optional plugin (MCP, or a plugin's C4 tool calls) |
| Fossil and Perforce | `sourceControlProviders` plugins (P2) |
| `restart` self-development tool | F-OPS-27 (rebuild-relaunch with resume) |
| Calculator/Notes/Bookmarks plugins | Bookmarks is core editor (F-EDT-52); scratchpad replaces Notes |

## Migration (`ied import legacy`)

A one-shot importer for legacy workspaces, safe to run repeatedly (idempotent, read-only on legacy files). `<legacy>` below is the predecessor's hidden per-workspace folder:

| From | To | Notes |
|---|---|---|
| Predecessor app config endpoints | model profiles (+ capability probe) | API keys -> Windows Credential Manager; headers kept |
| Legacy routing (chat/research/vision/inline) | a model group "imported-legacy" with matching roles | |
| `<legacy>/skills/<name>/SKILL.md` | `.ied/skills/<name>/SKILL.md` | frontmatter compatible (`name`, `description`, `triggers`); the C4 tree's legacy workspace alone had 156 |
| `<legacy>/agent-modes.json` | `.ied/modes/*.json` | tool names mapped; path globs kept |
| `<legacy>/workspace.json` LSP/DAP enablement | `.ied/solution.json` / project settings | C4 profile replaced by the `c4` plugin's |
| `<legacy>/chat-workspace.json` + `trajectories/*.jsonl` | read-only "Imported" chats | browsable and searchable; forking one starts a new IED chat with its context |
| Saved terminal commands | saved commands (F-TRM-26) | |

Legacy workspace folders stay where they are; IED never writes to them (owner note: legacy files remain, C4 no longer depends on them).
