# Features: chat (CHT)

Owner: `plugins/chat` - its UI, its default tool set and the runtime (`code/`, a built-in on the plugin API, `docs/26`). Design: `docs/04` (AI design), `docs/12` (agent runtime), `docs/13` (tools).

## Tabs and layout

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-1 | Browser-like tab strip per chat group: new tab (Ctrl+T inside the chat view where the browser allows; "+" button), close, reorder, middle-click close, tab title auto-generated then editable | P0 | US-CHT-1 |
| F-CHT-2 | Tab state glyphs: running (animated), waiting for approval (attention), done-unread, error, interrupted; task progress `3/7` on the tab when a task list exists | P0 | US-CHT-1, US-CHT-9 |
| F-CHT-3 | Dock chats side by side / stacked (shell docking, F-SHL-10); no floating windows | P0 | US-CHT-1 |
| F-CHT-4 | Chat list / history panel: all chats of the workspace with search, filter (running, archived), pin, archive, delete (to trash) | P1 | US-CHT-16 |
| F-CHT-5 | Generic (no-project) chats: app-tier chats available from any workspace and from the start page; limited tool set (web, scratch dir, no workspace FS) | P0 | US-CHT-10 |
| F-CHT-6 | Cross-workspace "what's running" view: every active run on the server with status, model, elapsed, last tool | P1 | US-SRV-7 |

## Message stream rendering

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-10 | Live streaming of thinking, text and tool calls (arguments stream as they are generated, pretty-printed once complete) | P0 | US-CHT-2 |
| F-CHT-11 | Thinking blocks: collapsible, collapsed by default after completion, duration + tokens shown in the header | P0 | US-CHT-2 |
| F-CHT-12 | Tool call cards: tool name + source (built-in / plugin / MCP server), compact argument summary, status (pending approval / running / ok / error / cancelled), elapsed time, expandable full args + result; results render by type (diff, file excerpt, terminal output, image, table, markdown) | P0 | US-CHT-2 |
| F-CHT-13 | Per-segment metadata: duration, input/output tokens, tokens/sec, model that produced it, routing note if rerouted | P0 | US-CHT-2 |
| F-CHT-14 | Markdown rendering with code blocks (copy, insert at caret, apply to file -> diff), file paths as links (open in editor), mermaid | P0 | - |
| F-CHT-15 | Turn footer: stop reason in plain words ("finished", "stopped by you", "hit max tokens - continued 1x", "stream idle 90 s - retried", "model ended after announcing an action - nudged") | P0 | US-AGT-1 |
| F-CHT-16 | Sub-agent sections: collapsible nested transcript per sub-agent with its role, model, status and report; "open as tab" | P1 | US-AGT-2 |
| F-CHT-17 | Large transcripts virtualized; older turns lazy-load from the server on scroll (memory eviction) | P0 | US-CHT-6 |
| F-CHT-18 | Media: images/video in messages as thumbnails with a lightbox; tool-produced media rendered inline | P1 | US-CHT-18 |

## Input bar

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-20 | Multi-line input; Enter sends, Shift+Enter newline (setting to swap); history (Up on empty input) | P0 | - |
| F-CHT-21 | `@` mentions: fuzzy file/folder paths (workspace index), symbols (`@#Name` via LSP workspace symbols), open documents, terminals, output sources, Error List, git diff (`@diff`), other chats | P0 (files), P1 (rest) | US-CHT-4 |
| F-CHT-22 | Attachments: drag-drop, paste (images from clipboard), picker; size limits shown; vision-only attachments warn when the selected group has no vision model | P0 | US-CHT-4, US-CHT-18 |
| F-CHT-23 | Model group selector + effort selector + mode selector (Ask / Plan / Agent / custom) + permission mode, all per chat, persisted | P0 | US-CHT-3, US-CHT-13 |
| F-CHT-24 | Privacy toggle (local-only) with a lock indicator; shows "locked: touched sensitive file X" when tainted | P0 | US-MDL-5 |
| F-CHT-25 | Slash commands: `/compact`, `/clear`, `/fork`, `/rewind`, `/mode`, `/model`, `/skill <name>`, `/task`, plugin-contributed commands | P1 | - |
| F-CHT-26 | Send while busy: **Queue** (next turn) or **Inject** (delivered at the next tool boundary as steering) - two buttons; queued items shown above the input and editable/removable | P0 (inject), P1 (queue) | US-CHT-5, US-CHT-15 |
| F-CHT-27 | Stop (Esc in the chat): cancels the model stream and running tools (with kill of their process trees) and records what was done | P0 | US-CHT-11 |
| F-CHT-28 | Voice input relies on the OS keyboard on phones; no custom STT in v1 | P2 | - |

## Chat bar telemetry

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-30 | Bar under the input: session tokens in/out, current tokens/sec (live while streaming), time to first token, context fill bar (% of window) | P0 | US-CHT-3 |
| F-CHT-31 | Cloud usage widget when a cloud model is in the group: e.g. Claude 5-hour and 7-day utilization with reset countdowns, or API remaining tokens/requests and session cost | P1 | US-CLD-2 |
| F-CHT-32 | Model status chips: which models the group uses, busy/idle/unreachable, queue position when waiting for a local slot | P1 | US-MDL-4 |

## Right-side breakdown panel

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-40 | Collapsible right panel (per chat) with sections, each collapsible | P1 | US-CHT-7 |
| F-CHT-41 | Context usage: stacked bar + table by category (system prompt, AGENTS.md, plugin sections, skills loaded, conversation, tool results, attachments, compressed summaries); click a category to see items and their token counts | P1 | US-CHT-7 |
| F-CHT-42 | Compression log: each compression event (when, trigger, before/after tokens, which model summarized), with a link to view the summary text | P1 | US-CTX-1 |
| F-CHT-43 | Modified files: list with +/- line counts, per-file diff, revert file, open; grouped by turn | P1 | US-CHT-7 |
| F-CHT-44 | Tool stats: counts, failures, total time per tool; slowest calls | P1 | US-CHT-7 |
| F-CHT-45 | Tasks (milestones) - see below; Scratchpad shortcut; sub-agents list | P1 | US-CHT-9 |
| F-CHT-46 | Run info: mode, permission mode, budgets and consumption, models used, cost (cloud), wall time | P1 | US-AGT-6 |

## Approvals and questions

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-50 | Approval cards for gated tool calls: shell command (with cwd, env diff), file write (diff), network grant, plugin install; actions: Allow once / Allow for this chat (pattern) / Always allow (writes a rule at chosen tier) / Deny with message | P0 | US-CHT-14 |
| F-CHT-51 | Question cards (`ask_user` tool): question + options (+ free text), answerable from any device; the run waits; notification sent | P1 | US-TOOL-8 |
| F-CHT-52 | Plan approval (Plan mode output): plan rendered with "Approve and implement" / "Approve, implement and commit" / "Revise" buttons that switch mode and continue | P0 | US-AGT-3 |
| F-CHT-53 | Pending approvals aggregate in the notification center and on the workspace switcher badge | P0 | US-SRV-6 |

## Editing history

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-60 | Edit a past user message and resend (truncates after it; the old branch is kept as a fork) | P0 | US-CHT-11 |
| F-CHT-61 | Regenerate last answer (optionally with another model group) | P0 | US-CHT-11 |
| F-CHT-62 | Fork chat from any message into a new tab | P0 | US-CHT-11 |
| F-CHT-63 | Checkpoints: workspace file state snapshot per turn for AI-touched files; *Rewind* to before a turn: code only / conversation only / both, with preview of what will change | P1 | US-CHT-12 |
| F-CHT-64 | Delete message(s) from the context (keeps them in the raw log, marked excluded) | P1 | - |

## Scratchpad

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-70 | Per-chat scratchpad (markdown document) owned by the chat; opens inline (a collapsible pane inside the chat view) or as a docked editor tab beside the chat | P1 | US-CHT-8 |
| F-CHT-71 | AI access via `scratchpad` tool (read / write section / append / replace); human edits are visible to the AI on its next turn (diff summarized in a system note) | P1 | US-CHT-8 |
| F-CHT-72 | The scratchpad is never compressed away: its current content is part of every request (bounded; overflow -> AI asked to tidy it) | P1 | US-CHT-8 |
| F-CHT-73 | Promote scratchpad to a file (e.g. `docs/plan-x.md`) | P2 | - |

## Tasks and milestones

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-80 | Per-chat task list: milestones -> tasks, statuses (pending / in progress / done / blocked / skipped), notes, owner (main agent or sub-agent) | P1 | US-CHT-9 |
| F-CHT-81 | AI tools to create/update/complete tasks (T-META-*); the agent is prompted to keep exactly one task in progress | P1 | US-CHT-9 |
| F-CHT-82 | Human edits (reorder, add, mark done/skip) are fed back to the agent as steering | P1 | US-CHT-9, US-CHT-5 |
| F-CHT-83 | Task list pinned under the latest message and in the right panel; progress on the tab and in the "what's running" view | P1 | US-CHT-9 |
| F-CHT-84 | Export task list to markdown checklist; import a markdown checklist as tasks | P2 | - |

## History, export, trajectory

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-CHT-90 | Full-text search across chats (workspace and app tier) with jump to message | P1 | US-CHT-16 |
| F-CHT-91 | Export chat to markdown (with or without tool details) | P1 | US-CHT-16 |
| F-CHT-92 | Trajectory viewer: raw event stream per chat (requests, raw chunks, tool calls/results, compression, routing decisions, stall detections), filters, search, export; retention limits (F-AIR-64) | P1 | US-CHT-17, US-AGT-1 |
| F-CHT-93 | "Report a stop": one click bundles the last request/response pair from the trajectory into a file for diagnosing harness vs model | P1 | US-AGT-1 |
