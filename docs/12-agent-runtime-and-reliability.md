# 12 - Agent runtime and reliability

## Purpose

Specify how an AI run executes on the server: the loop, modes, sub-agents, tasks, steering, checkpoints - and, above all, **how IED keeps local models from "stopping randomly"** (IDEplan: "i dont know if that was the model its self or the harness but we should have some protections for that"). The answer to "model or harness?" must always be visible to the owner.

This doc draws on a code-level review of Echo's harness (`C:\GameDev\echo`, the IDE the owner saw the stops in). Its failure causes are listed below with the fix IED applies to each.

## Vocabulary

- **Chat** - a persistent conversation (tab).
- **Turn** - one user message and everything the agent does in response until it yields back.
- **Step** - one model request/response inside a turn, plus the tool calls it made.
- **Run** - the execution of a turn on the server (has a state machine, survives the browser, may be interrupted by a server restart).

## Run state machine

```
            +-----------+   approval/question   +--------------------+
  send ---> |  running  | --------------------> | awaiting_user      |
            |           | <-------------------- | (approval|answer)  |
            +-----------+       answered        +--------------------+
               |   |  ^
   stop/budget |   |  | resume
               v   |  |
            +--------+                          +--------------------+
            | paused |  <---- server restart -- | interrupted        |
            +--------+        (auto/ask/never)  +--------------------+
               |
     finish /  v  error (after policy exhausted)
            +--------+    +--------+
            |  done  |    | failed |
            +--------+    +--------+
```

Every transition is journaled to the chat's step log before it takes effect (`.ied/local/chats/<chatId>/steps.jsonl`), so a crash at any point leaves a recoverable state.

## The loop

```
run(turn):
	ctx = assemble_context(chat)                      # docs/04 system message order, ledger, compression check
	loop:
		if budgets_exceeded(): pause("budget"); return
		deliver_pending_steering(ctx)                 # injected user messages, task edits, scratchpad edits
		req = build_request(ctx, mode.tools, model_for(role="primary"))
		resp = stream_with_guards(req)               # watchdogs, dialect parsing, repair  (below)
		record(resp)
		classify(resp) ->
			tool_calls      : results = execute(resp.tool_calls)       # permissions, approvals, parallel-safe batches
			                  record(results); continue
			finish_called   : verify_or_nudge(); if accepted: done(); return
			looks_complete  : if mode in (ask, plan): done(); return
			                  else nudge("if finished call finish(...), else continue"); continue
			premature_stop  : apply recovery policy (below); continue or fail
```

- **Ask mode:** a text reply with no tool calls ends the turn, *unless* the stop is classified as premature (below). This is the main Echo gap - there, any reply without tool calls ended the turn in every mode except Goal.
- **Agent mode:** the turn ends only via `finish(summary, verification)`. A reply with no tool call and no `finish` is a checkpoint followed by a continuation nudge (bounded; if the model keeps replying without acting, it is paused as "no progress" - never silently "done").
- **Plan mode:** read-only tool set + `ask_user` + `research`; ends with a plan (`submit_plan(plan_markdown, tasks[])`) that the UI renders with approve/revise buttons (F-CHT-52).

## Reliability: why runs stop, and what IED does about each

### Taxonomy

A stop is classified into exactly one **stop reason**, shown in the turn footer (F-CHT-15) and recorded in the trajectory:

| Layer | Stop reason | Detection |
|---|---|---|
| Transport | `connect_failed`, `http_error(code)`, `disconnected_mid_stream` | socket/HTTP errors; EOF before a terminal chunk |
| Server stall | `stalled_prefill`, `stalled_stream` | watchdogs (below) |
| Model output | `max_tokens` | `finish_reason=length` (or `max_tokens` / dialect equivalent) |
| Model output | `empty`, `reasoning_only` | no content and no tool calls; or only reasoning |
| Model output | `announced_no_action` | final text ends by announcing an action ("Now I'll edit X:", "Let me run the tests.") with no tool call |
| Model output | `unparsed_tool_call` | tool-call markup in text (`<tool_call>`, `<function=`, `[TOOL_CALLS]`, fenced JSON with `name`+`arguments`) that the server did not parse |
| Model output | `malformed_tool_call` | arguments not valid JSON after repair, or schema-invalid |
| Model output | `unknown_finish(<value>)` | finish reasons outside the known set (`eos`, `abort`, `end_turn`...) - treated as "ended", then classified by content |
| Context | `context_overflow` | provider error mentioning context length, or prompt > discovered `n_ctx` |
| Agent | `loop_detected`, `no_progress`, `budget` | runtime checks |
| User | `user_cancelled` | stop button |

### Recovery policy (defaults; per-model overrides in the model profile)

| Stop reason | Action | Bound |
|---|---|---|
| `connect_failed`, `http_error 5xx/429` | retry with exponential backoff + jitter; 429 honors `retry-after`; after 2 failures try the role's fallback model (if privacy allows) | 4 attempts |
| `disconnected_mid_stream` | keep the partial output; **continue** (prefix continuation if the dialect supports it - e.g. vLLM `continue_final_message`, raw `/completion` prompts - else re-request with the partial text as an assistant message + a short "continue exactly where you stopped" note) | 2 per step |
| `stalled_prefill` / `stalled_stream` | cancel the request, then same as disconnected | 2 per step |
| `max_tokens` | continue (as above); if the cut happened inside a tool call's arguments, drop the partial call and ask for the call again | 3 per step |
| `empty` / `reasoning_only` | re-request with a nudge ("Your previous reply contained no answer and no tool call. Respond now: either call a tool or answer."); on the 2nd occurrence, also lower effort / disable thinking for that step | 2 per step |
| `announced_no_action` | nudge ("You said you would <x> but did not call a tool. Call the tool now, or explain why not.") | 2 per step |
| `unparsed_tool_call` | parse it with the text fallback parser; if it parses, execute it as a normal tool call (and mark the dialect so future steps parse proactively) | - |
| `malformed_tool_call` | return a tool error result with the validation message and the schema; the model fixes it | 3 per call id |
| `context_overflow` | read the real limit from the error, update the model profile, compress, retry | 3 per step |
| `loop_detected` | pause with explanation (same call + same error 3x, or identical edits oscillating) | - |
| any reason after its bound | **pause** (not fail) with the reason and an "attention" notification; the owner can resume, switch model, or edit | - |

Nudges are **user-role** messages tagged `origin: harness` (rendered in the UI as small grey "harness" notes so the owner sees exactly what IED said to the model). No mid-history `system` messages (they break strict chat templates - an Echo issue).

### Watchdogs (the "is it dead or thinking?" problem)

Two separate timers, because local prefill of a long prompt can legitimately take minutes with no bytes on the wire:

- **Prefill watchdog (time to first token):** budget = `base (30 s) + promptTokens / prefillRate * 2.0`, where `prefillRate` is measured per model from previous responses (llama.cpp returns `timings.prompt_per_second`; otherwise from measured TTFT). If the server streams progress or keepalive comments during prefill, each one extends the deadline. The UI shows "processing prompt (est. 45 s)" with a progress bar from the estimate.
- **Idle watchdog (between chunks after the first token):** default 60 s for local, 120 s for cloud; resets on any byte (including SSE comments).
- **Hard cap per step:** default 30 min (configurable), so nothing hangs forever.

### Truth about the model's context window

Echo defaulted every model to a 262144-token window. When the real server was smaller, the server silently truncated the prompt (Ollama `num_ctx`, llama.cpp context shift), the model lost its instructions, and behavior degraded into "random" stops. IED:

1. Discovers the real context size (llama.cpp `/props`, Ollama `/api/show`, vLLM `max_model_len` in `/v1/models`) on connect and on every probe.
2. Refuses to send a prompt larger than the discovered window (compresses first).
3. Warns in the model page when the configured window differs from the discovered one.
4. Detects context shift: if the server reports `n_past`/truncation flags or usage shows fewer prompt tokens than sent, flag `context_truncated_by_server` in the trajectory and the UI.

### Dialect normalization (the other big source of "stops")

- **Tool calls arriving as text:** a fallback parser recognizes Hermes `<tool_call>{...}</tool_call>`, Qwen-style `<function=name>...`, Mistral `[TOOL_CALLS]`, fenced JSON objects with `name` + `arguments`, and harmony channels. Streaming detection: once a tool-call opener is seen in text, the UI renders a tool card instead of prose.
- **Streaming tool-call deltas** merge by `id` when present, by `index` otherwise (Echo merged by index only and corrupted servers that send several calls at index 0 with different ids).
- **Reasoning** from `reasoning_content`, `reasoning`, `thinking`, or `<think>...</think>` in content - normalized into thinking segments; a reply that is only an unclosed `<think>` is `reasoning_only`.
- **Malformed SSE chunk:** logged and skipped; the stream continues (Echo aborted the whole stream on one bad chunk).
- **`[DONE]` without finish_reason** is accepted; **EOF without either** is `disconnected_mid_stream` (recoverable), not a fatal error.

### Transparency (answering "model or harness?")

- Turn footer shows the stop reason and every recovery action taken ("continued after max_tokens x1; retried after 60 s idle x1").
- The trajectory stores the exact request body, raw response chunks (batched), timings (TTFT, prefill rate, tokens/s) and the classification decision.
- "Report a stop" (F-CHT-93) bundles the last exchange so it can be replayed against the same server outside IED (curl script included). If the replay stops the same way, it is the model/server; if not, it is IED.

### Echo failure causes -> IED fix (checklist for the implementer)

| # | Echo behavior | IED fix |
|---|---|---|
| 1 | Reply without tool calls ends the turn outside Goal mode | Mode-aware completion; `announced_no_action` detection; Agent mode needs `finish` |
| 2 | No fallback parser for tool calls emitted as text | Text tool-call parser + dialect profile |
| 3 | Tool-call deltas merged by index only | Merge by id, then index |
| 4 | Mid-stream disconnect / malformed chunk / missing `[DONE]` after content is fatal | Continue-from-partial recovery; skip bad chunks |
| 5 | Unknown finish reasons fatal | Treat as ended; classify by content |
| 6 | Default 262k context vs smaller real window -> silent truncation | Discover real `n_ctx`; never exceed it; detect server-side truncation |
| 7 | Idle/header timeout fires during long prefill | Separate prefill watchdog scaled by measured prefill rate |
| 8 | Mid-history `system` messages and `name` fields break strict templates | User-role harness notes; template hygiene per dialect |
| 9 | `MaxChatRounds` never enforced; no loop detection | Budgets + loop/no-progress detection |
| 10 | Vision rerouting switches the whole turn's model | Route only the step that needs vision; keep the primary model for the rest |
| 11 | No network retry | Backoff retry with fallback model |

## Tools execution

- Tool calls in one step execute **in parallel** when every call in the batch is declared `parallelSafe` (reads, searches, LSP queries); otherwise sequentially in the order given.
- Each call: permission check (`docs/11`) -> approval wait if needed -> execute with timeout -> result (truncated to the tool's result budget, with a "result truncated; use <x> to read more" note) -> journal.
- **Side-effect safety on resume:** each call is journaled `started` before execution and `finished` after. On resume after a crash, a call that is `started` but not `finished` is **not re-executed**; the model receives "this call was interrupted by a server restart; its effect is unknown - verify before retrying."
- Shell commands run in the chat's AI terminal (F-TRM-23); long-running ones can be backgrounded with a handle.

## Steering, queueing and task edits

- **Inject** (F-CHT-26): delivered at the next step boundary (after the current tool batch finishes, or immediately if the model is mid-stream and the owner chose "interrupt") as a user-role note: `[Steering from the user, sent while you were working]: ...`.
- **Queue:** held until the turn ends, then sent as the next turn.
- **Task list / scratchpad edits by the owner** are summarized into a steering note ("The user marked task 3 done and added task 'Update docs'").

## Sub-agents

- Tool: `spawn_agents([{ role: research|worker|<custom>, task, tools?, paths?, budget? }], wait: bool)`, plus `agent_send`, `agents_wait`, `agents_cancel`.
- Each sub-agent: own transcript (stored under the parent chat), model + temperature from its role in the active group, restricted tool set (research = read-only + web; worker = read + write within assigned paths, no git mutations by default), its own budgets, a required `report(summary, artifacts)` at the end.
- **Concurrency** is bounded by the scheduler (model slots), not by a fixed number; a group whose worker model has 4 llama.cpp slots can run 4 workers in parallel.
- Parent receives bounded reports (the full sub-transcript stays available via `history_read`). If the parent does not wait, reports arrive as steering notes when ready.
- UI: collapsible nested sections (F-CHT-16), expandable into tabs.
- **Planner/implementer pattern** (J3): the `planning` role produces `submit_plan` with tasks; Agent mode then dispatches tasks to `implementation`/`worker` roles and verifies with build/test tools on the primary. This is a built-in mode preset ("Plan -> Implement"), not a special code path.

## Tasks and milestones

- The task list is chat state (F-CHT-80), mutated by tools `tasks_set(items)`, `task_update(id, status, note)`, and by the owner.
- Agent mode's prompt asks the model to keep exactly one task `in_progress`. The runtime does not enforce ordering, but a turn that ends via `finish` with open tasks gets one nudge ("tasks X, Y are still open - finish them or mark them skipped with a reason").

## Checkpoints and rewind

- Before a step's first write to a file, the runtime snapshots the file's current content (content-addressed blob in `.ied/local/checkpoints/`), tagged with turn id.
- Rewind (F-CHT-63) restores files to the state before a chosen turn (only files the AI touched; files the owner changed since are shown as conflicts with a per-file choice), and/or truncates the conversation.
- Checkpoints are pruned with the chat (retention setting), never committed.

## Budgets

Per run (from mode defaults, overridable per chat): max steps, max tokens (in+out), max wall time, max cost (cloud), max tool calls. At 80%: a harness note tells the model to wrap up. At 100%: pause with a summary and a "+50% and continue" button.

## Restart behavior

On server start, runs in `running` or `awaiting_user` are restored:

- `awaiting_user` stays awaiting (approval cards come back).
- `running` becomes `interrupted`; policy `ai.resumeInterrupted = ask | auto | never` (default `ask`, `auto` for Agent mode). Resume starts a fresh step from the journal (the in-flight model request is lost; its partial text is kept as an assistant message only if it was a complete segment).

## Harness test strategy (summary; details in `docs/19`)

A **fake model server** (OpenAI-compatible, scriptable) is a first-class test fixture: scripts like "stream 200 tokens then stall 120 s", "emit Hermes tool call as text", "finish_reason=length mid-JSON", "send malformed chunk", "return context-length error with limit 32768", "close socket after 50 chunks". Every row in the taxonomy table has a regression test. Recorded real-model trajectories can be replayed through the same fixture.
