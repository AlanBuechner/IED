# 12 - Agent runtime and reliability

## Purpose

Specify how an AI run executes on the server: the loop, modes, sub-agents, tasks, steering, checkpoints - and, above all, **how IED keeps local models from "stopping randomly"** (IDEplan: "i dont know if that was the model its self or the harness but we should have some protections for that"). The answer to "model or harness?" must always be visible to the owner.

This doc draws on a code-level review of the predecessor IDE's harness (the IDE the owner saw the stops in). Its failure causes are listed below with the fix IED applies to each.

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

- **Ask mode:** a text reply with no tool calls ends the turn, *unless* the stop is classified as premature (below). This is the predecessor's main gap - there, any reply without tool calls ended the turn in every mode except Goal.
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
| Model output | `repetition` | the reply's thinking, prose or a call's arguments end in one passage repeated over their last 4 KiB (8 KiB for arguments), checked as they stream (`plugins/chat/code/repetition.ch`) |
| Model output | `announced_no_action` | final text ends by announcing an action ("Now I'll edit X:", "Let me run the tests.") with no tool call |
| Model output | `unparsed_tool_call` | tool-call markup in text (`<tool_call>`, `<function=`, `[TOOL_CALLS]`, fenced JSON with `name`+`arguments`) that the server did not parse |
| Model output | `malformed_tool_call` | arguments not valid JSON after repair, or schema-invalid |
| Model output | `unknown_finish(<value>)` | finish reasons outside the known set (`eos`, `abort`, `end_turn`...) - treated as "ended", then classified by content |
| Context | `context_overflow` | provider error mentioning context length, or prompt > discovered `n_ctx` |
| Agent | `loop_detected`, `no_progress`, `budget` | runtime checks |
| User | `user_cancelled` | stop button |
| Harness | `harness_error` | the turn's own code let an exception escape: the turn ends with a note saying so, and the server's log has the message and where it was thrown (`Logs` below) |

### Recovery policy (defaults; per-model overrides in the model profile)

| Stop reason | Action | Bound |
|---|---|---|
| `connect_failed`, `http_error 5xx/429` | retry with exponential backoff + jitter; 429 honors `retry-after`; after 2 failures try the role's fallback model (if privacy allows) | 4 attempts |
| `disconnected_mid_stream` | keep the partial output; **continue** (prefix continuation if the dialect supports it - e.g. vLLM `continue_final_message`, raw `/completion` prompts - else re-request with the partial text as an assistant message + a short "continue exactly where you stopped" note) | 2 per step |
| `stalled_prefill` / `stalled_stream` | cancel the request, then same as disconnected | 2 per step |
| `max_tokens` | continue (as above); if the cut happened inside a tool call's arguments, drop the partial call and ask for the call again | 3 per step |
| `empty` / `reasoning_only` | re-request with a nudge ("Your previous reply contained no answer and no tool call. Respond now: either call a tool or answer."); on the 2nd occurrence, also lower effort / disable thinking for that step | 2 per step |
| `repetition` | stop the stream, discard the reply, and nudge ("Your last reply was stopped and discarded because it kept repeating the same text..."): a loop sampled once is rarely sampled again, while continuing from it repeats it | 2 per step |
| `announced_no_action` | nudge ("You said you would <x> but did not call a tool. Call the tool now, or explain why not.") | 2 per step |
| `unparsed_tool_call` | parse it with the text fallback parser; if it parses, execute it as a normal tool call (and mark the dialect so future steps parse proactively) | - |
| `malformed_tool_call` | return a tool error result with the validation message and the schema; the model fixes it | 3 per call id |
| `context_overflow` | read the real limit from the error, update the model profile, compress, retry | 3 per step |
| `loop_detected` | pause with explanation (same call + same error 3x, or identical edits oscillating) | - |
| any reason after its bound | **pause** (not fail) with the reason and an "attention" notification; the owner can resume, switch model, or edit | - |

Nudges are **user-role** messages tagged `origin: harness` (rendered in the UI as small grey "harness" notes so the owner sees exactly what IED said to the model). No mid-history `system` messages (they break strict chat templates - a predecessor issue).

### Watchdogs (the "is it dead or thinking?" problem)

Two separate timers, because local prefill of a long prompt can legitimately take minutes with no bytes on the wire:

- **Prefill watchdog (time to first token):** budget = `base (30 s) + promptTokens / prefillRate * 2.0`, where `prefillRate` is measured per model from previous responses (llama.cpp returns `timings.prompt_per_second`; otherwise from measured TTFT). If the server streams progress or keepalive comments during prefill, each one extends the deadline. The UI shows "processing prompt (est. 45 s)" with a progress bar from the estimate. As built: a llama.cpp server (the probe says so) is asked for `return_progress`, and its `prompt_progress {total, cache, processed, time_ms}` reports extend the deadline like keepalives; the turn keeps the latest as `prefill` (in the chat's snapshot, so a client that joins mid-prefill sees it) and publishes `turn.progress {turnId, prefill}` at most every 250 ms, `prefill: null` when the first token comes or the request ends. The chat shows "Reading the prompt" with the share read, the tokens and the time. Another server's read is estimated (the prompt's tokens at the measured rate) and, when it should take 3 s or more, published as `prefill: {estimated: true, total, estimatedMs, startedMs}`; the chat shows "about 45 s for 12k tokens (estimated)" with a bar moving with the clock.
- **Idle watchdog (between chunks after the first token):** default 60 s for local, 120 s for cloud; resets on any byte (including SSE comments).
- **Hard cap per step:** default 30 min (configurable), so nothing hangs forever.

### Truth about the model's context window

The predecessor IDE defaulted every model to a 262144-token window. When the real server was smaller, the server silently truncated the prompt (Ollama `num_ctx`, llama.cpp context shift), the model lost its instructions, and behavior degraded into "random" stops. IED:

1. Discovers the real context size (llama.cpp `/props`, Ollama `/api/show`, vLLM `max_model_len` in `/v1/models`) on connect and on every probe.
2. Refuses to send a prompt larger than the discovered window (compresses first).
3. Warns in the model page when the configured window differs from the discovered one.
4. Detects context shift: if the server reports `n_past`/truncation flags or usage shows fewer prompt tokens than sent, flag `context_truncated_by_server` in the trajectory and the UI.

### Dialect normalization (the other big source of "stops")

- **Tool calls arriving as text:** a fallback parser recognizes Hermes `<tool_call>{...}</tool_call>`, Qwen-style `<function=name>...`, Mistral `[TOOL_CALLS]`, fenced JSON objects with `name` + `arguments`, and harmony channels. Streaming detection: once a tool-call opener is seen in text, the UI renders a tool card instead of prose.
- **Streaming tool-call deltas** merge by `id` when present, by `index` otherwise (the predecessor merged by index only and corrupted servers that send several calls at index 0 with different ids).
- **Reasoning** from `reasoning_content`, `reasoning`, `thinking`, or `<think>...</think>` in content - normalized into thinking segments; a reply that is only an unclosed `<think>` is `reasoning_only`.
- **Malformed SSE chunk:** logged and skipped; the stream continues (the predecessor aborted the whole stream on one bad chunk).
- **`[DONE]` without finish_reason** is accepted; **EOF without either** is `disconnected_mid_stream` (recoverable), not a fatal error.

### Transparency (answering "model or harness?")

- Turn footer shows the stop reason and every recovery action taken ("continued after max_tokens x1; retried after 60 s idle x1").
- The trajectory stores the exact request body, raw response chunks (batched), timings (TTFT, prefill rate, tokens/s) and the classification decision.
- "Report a stop" (F-CHT-93) bundles the last exchange so it can be replayed against the same server outside IED (curl script included). If the replay stops the same way, it is the model/server; if not, it is IED.

### Predecessor failure causes -> IED fix (checklist for the implementer)

| # | Predecessor behavior | IED fix |
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

Steps are not capped unless the owner asks: `ai.maxSteps` is 0 (no limit) by default, so a long Agent turn runs until it calls `finish`, the owner stops it, or loop and no-progress detection pause it (the `loop_detected` and `no_progress` rows). A number caps the model requests of one turn, with the 80% note and the `budget` pause.

## Restart behavior

On server start, runs in `running` or `awaiting_user` are restored:

- `awaiting_user` stays awaiting (approval cards come back).
- A chat's files are read as UTF-8 whatever they hold: one written before C4 BUG-384 kept a console's code-page bytes
  (a tool's output) as they were, and the JSON reader refused it whole, so a restarted chat came back without the
  turn it was running; such bytes now read as U+FFFD.
- `running` becomes `interrupted`: a call that was running gets a result saying its effect is unknown, and the chat
  waits paused.
- **Going on by itself** (`plugins/chat/code/resume.ch`, built 2026-10-23): once the workspace has opened (3 s later, so
  its plugins have settled), a chat goes on with a turn IED starts, its message IED's - the transcript heads it "IED,
  after a restart" (or "after relaunching"), and `user.resumed` names why (`docs/10`):
  - `relaunch`: a call of the chat's turn restarted the server on purpose (`IED::ToolResult::restarts`; developer
    mode's `ied_relaunch`, `docs/28`), whether that turn ended before the restart or was cut off. The chat's header
    keeps the mark (`resume`, the tool's name) until a turn starts. The model is told "IED restarted onto its new
    build, as your call to <tool> asked, and this chat continues on it. Pick up the task where it stopped: check that
    what you changed works now, then go on."
  - `interrupted`: the restart cut the turn off, and `ai.resumeInterrupted` (app or solution tier) allows it - `agent`
    (the default) an Agent-mode chat, `always` any chat, `never` none. The model is told the restart cut its turn off
    and to check the state of what it was doing before redoing anything. A chat whose last turns a restart cut off
    three times in a row, each one a turn IED started, waits for the owner: a turn that brings the server down does not
    do it forever. A turn that ends any other way starts the count again.
  A Claude chat's session (`docs/04` "A chat's session") resumes with the turn, so the model has the conversation up to
  the restart.

## Logs: why the server stopped

The server outlives every window, so when it stops, nobody watched it. Two logs in the machine-local folder
(`%LOCALAPPDATA%\IED\logs\`, `docs/21`) say what happened, one line each, `<date> <time> [<pid>] <text>`, appended
and closed at once (the last line before a crash is on disk); a log past 4 MiB moves to `<name>.1.log` when its
program starts (`launcher/logfile.ch`, which the server and the app share):

- **`server.log`** (in the server's data folder: `--data`'s `logs\` when it is given): each run's start (version,
  program, data and home folders), the address it listens on, every line of the Output window's Server source (plugins
  started, workspaces opened, what a plugin says), a start that did not happen and why (`not started: another
  ied-server runs ...`), `stopped by the control channel` and `exit: the HTTP server stopped`. The server names it its
  crash log (`Platform::SetCrashLog`, C4's `library/bedrock.md` "Crash reports"), so a fatal error - an exception
  nothing caught on any thread, the collector's own, a fault nothing handled - ends the run with the report: the
  message, the thread's id and name, and the stack, named from `ied-server.pdb` (the install keeps it beside the
  program, `docs/21`).
- **`desktop.log`**: each start of the app, each server it started (`started ied-server, pid N`), how each ended
  (`the server (pid N) stopped answering: it exited with code 1; its log says why: ...`, from the exit code of the
  process it started; a server another program started is only said to be gone), each return (`the server is back`)
  and a start that failed with its reason; the app's own crash report.

Plugin code does not end the server this way: a thread a plugin starts (`PluginContext::StartThread`) that lets an
exception escape ends alone, and a chat turn's ends the turn (`harness_error`, with a note in the chat); both log
`plugin <id>: a thread of its own ended on an exception nothing caught: <message>` (or `chat <id>: turn <id> ended
...`) with the exception's type and the stack from its `throw` (`Platform::StackText` in the handler).

How a run ended reads off its last lines: `exit: the HTTP server stopped` is a stop someone asked for (`ied stop`, the
app's relaunch); a `C4: fatal:` report is a crash, with where it happened; nothing after the run's last lines is a
process another program ended (Task Manager, a script, `TerminateProcess`: the app's line gives its exit code). A
launcher that starts a server which ends before its control pipe opens says so with its exit code and the log
(`ied-server ended (code 3) before its control pipe opened; ...\server.log says why`). The checks: `test_control`
(the log through a run, a refused start, and `testhooks.crash` - an uncaught exception on a thread of its own - leaving
its report with the thread's name and the line that threw), `test_desktop` (the log of a server killed, then stopped,
and started again each time), the launcher's `log_lines` and `log_file` tests.

## Harness test strategy (summary; details in `docs/19`)

A **fake model server** (OpenAI-compatible, scriptable) is a first-class test fixture: scripts like "stream 200 tokens then stall 120 s", "emit Hermes tool call as text", "finish_reason=length mid-JSON", "send malformed chunk", "return context-length error with limit 32768", "close socket after 50 chunks". Every row in the taxonomy table has a regression test. Recorded real-model trajectories can be replayed through the same fixture.

## As built (M4, first slice, 2026-10-21)

`plugins/chat/code/run*.cf` (`docs/04` "As built"). Built: the loop with Ask, Plan and Agent modes (Agent ends with `finish`, open tasks get one nudge); every row of the taxonomy except `budget` by tokens or cost and `context_truncated_by_server`; the recovery policy with these bounds - transport errors retried with backoff (`Retry-After` honored, at most 60 s) and the role's next model after 2 failures, 4 attempts in all; cuts (`disconnected_mid_stream`, `stalled_*`, `step_cap`) continued from the partial text, 2 per step; `max_tokens` 3, a call cut inside its arguments asked for again; `empty`, `reasoning_only`, `announced_no_action`, `unparsed_tool_call` nudged 2 times; `repetition` (a reply ending in one passage repeated, found as it streams) cut, discarded and nudged 2 times; `malformed_tool_call` answered with the schema, paused after 3 in a row; `context_overflow` learns the limit from the error, compresses and retries (a retry when the prompt fits the learned limit and nothing is left to compress), 3 per step; `loop_detected` after the same call failed the same way 3 times; `no_progress` after 3 Agent replies without a call or `finish`; the step budget `ai.maxSteps` (a note at 80%). A reason past its bound pauses the chat with a note saying why; the next message continues. Both watchdogs and the hard cap; the prefill budget uses the rate llama.cpp reports, else one measured from the time to first token, else the profile's `watchdog.prefillRate`. Discovery: each profile's server is asked once per server run - its slot context (llama.cpp `/props`, vLLM `max_model_len`) is the window of a profile that names none (and caps one that names a larger), and a llama.cpp server is asked for prompt progress. Steering (inject) at the next step - steering that arrives during what would be the turn's last answer gets one more step, and steering a stopped turn never took joins the queue; queued messages as the next turn. Restart: a turn in flight ends `interrupted`; its chat goes on by itself when a tool asked for the restart or `ai.resumeInterrupted` allows it ("Restart behavior"), else it waits paused.

Checked by `tools/server-checks/tests/test_chat.py` (24 checks) against `tools/fake-model`: every script of the fake model's library, the dialects, approvals, ask_user, steering, stopping, a fallback model, a server killed mid-turn.
