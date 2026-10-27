# fake-model - scriptable OpenAI-compatible model server

The fixture for the AI harness (`docs/12`, `docs/19`): an OpenAI-compatible `/v1/chat/completions` server whose every response is a script, so each stop class can be reproduced on demand. Stdlib only.

```bash
python -I tools/fake-model/fake_model.py --port 8089                 # prints LISTENING http://127.0.0.1:8089
python -I tools/fake-model/fake_model.py --list                      # the scripts and their stop classes
python -I tools/fake-model/probe.py --script stall-prefill --time-scale 0.05   # run one script, print the wire timeline
python -I tools/fake-model/probe.py --all                            # run every script and check its 'wire' block
python -I -m unittest discover -s tools/fake-model/tests -t tools/fake-model/tests
```

`probe.py` without `--url` starts the server in-process. With `--url` it probes any OpenAI-compatible server (llama.cpp, vLLM, Ollama), which makes it the "replay outside IED" half of "model or harness?" (`docs/12` Transparency).

## Choosing the script for a request

First match wins:

1. Header `X-Fake-Script: <name>`.
2. `"model": "script:<name>"` (or `"fake/<name>"`).
3. The FIFO filled by `POST /_fake/queue {"scripts": ["name", {inline script}, ...]}` - for harness tests that cannot set headers.
4. `--default-script` (default `ok-text`).

A script has one or more `responses`. Each request to a script uses the next response; the last one repeats forever. So `http-error-503` answers 503, 503, then succeeds, and `loop-same-call` returns the same call on every request. `POST /_fake/reset` rewinds every script and clears the queue and the request log.

## Options and endpoints

| Flag | Default | Meaning |
|---|---|---|
| `--n-ctx` | 32768 | total context; each slot gets `n_ctx / slots` (llama.cpp `-np` semantics) |
| `--n-ctx-train` | 131072 | reported as `meta.n_ctx_train` / Ollama `context_length` |
| `--slots` | 1 | concurrent requests; extra requests wait for a slot (look like a slow prefill) |
| `--time-scale` | 1.0 | multiplies every stall and chunk delay; per request: header `X-Fake-Time-Scale` |
| `--enforce-ctx` | off | reject prompts larger than the slot context (estimate: chars / 4) with a context error |
| `--ctx-error-style` | llamacpp | `llamacpp` (`exceed_context_size_error` with `n_ctx`) or `openai` (`context_length_exceeded`, limit in the message) |
| `metrics` (config, `off`) | off | `off`: `/metrics` answers 404, as a server that says nothing of itself. `busy`: a vLLM's `vllm:num_requests_running`, 1 while a completion is streaming - what a healthy long prefill looks like to the prefill watchdog. `idle`: the same gauges at 0 - a server alive but holding no such work. `slow`: the gauges at 1 after 3 s, past the 2 s the watchdog's own ask allows - a status page that times the ask out. See `stalled_prefill` below. |

| Endpoint | Returns |
|---|---|
| `POST /v1/chat/completions` | the script's response; `stream: true` = SSE over chunked HTTP, `false` = one `chat.completion` body |
| `GET /props` | llama.cpp: `default_generation_settings.n_ctx` (slot context), `total_slots` |
| `GET /slots` | llama.cpp: one entry per slot with `n_ctx`, `is_processing` |
| `GET /v1/models` | vLLM-style `max_model_len` (slot context) and llama.cpp `meta.n_ctx_train` |
| `POST /api/show` | Ollama: `model_info.<arch>.context_length`, `parameters: "num_ctx N"` |
| `GET /health` | `{"status": "ok"}` |
| `GET/POST /_fake/config` | read / change `nCtx`, `nCtxTrain`, `slots`, `model`, `timeScale`, `enforceCtx`, `ctxErrorStyle`, `defaultScript`, `vision` (what `/props` `modalities` says), `props` (false: no `/props`, a server that is not llama.cpp) |
| `POST /_fake/queue`, `POST /_fake/reset` | script FIFO, rewind |
| `GET /_fake/requests` | the last 500 requests: script, response index, estimated `promptTokens`, full body, outcome - assert what the harness sent (e.g. the continue note after a cut) |
| `GET /_fake/scripts` | name -> stop class + description |

## Script format

`scripts/<name>.json`:

```jsonc
{
	"description": "what the wire does",
	"stopClass": "stalled_prefill",            // a docs/12 stop reason, or "none" for negative tests
	"harness": "what IED's runtime must do",   // the expected recovery, quoting docs/12
	"responses": [
		{
			"headers": "immediate",               // or "lazy": no HTTP headers until the first body byte
			"chunkDelayMs": 15,                   // default delay between chunks (scaled by time-scale)
			"steps": [ { "stall": 120 }, { "role": "assistant" }, { "text": "late" }, { "finish": "stop" }, { "done": true } ],
			"wire": { "transport": "done", "minSilence": 120 }   // what probe.py must observe
		}
	]
}
```

Steps run in order; each has exactly one op:

| Op | Effect |
|---|---|
| `{"headers": true}` | send the HTTP 200 + SSE headers now |
| `{"role": "assistant"}` | the usual first chunk `delta: {role, content: ""}` |
| `{"text": "...", "chunk": 6, "delayMs": 15}` | `delta.content` in pieces |
| `{"reasoning": "...", "field": "reasoning_content"}` | reasoning deltas; `field` may be `reasoning` or `thinking` |
| `{"toolCall": {"name", "arguments", "id", "index", "idOn", "nameOn", "argChunk"}}` | native `tool_calls` deltas. `arguments` is an object or a raw (possibly broken) string; `idOn`/`nameOn`: `first` (default), `every`, `none` (id only); `index` defaults to the call's position |
| `{"textToolCall": {"dialect", "name", "arguments"}}` | the call as content text: `hermes`, `qwen`, `mistral`, `mistral-args`, `fenced-json`, `harmony` (see `dialects.py`) |
| `{"stall": 120, "keepalive": 10}` | silence for N seconds; with `keepalive`, an SSE comment every K seconds |
| `{"progress": 4, "delayMs": 200}` | llama.cpp's prompt progress (`prompt_progress {total, cache, processed, time_ms}` on an empty assistant delta, content null): N+1 reports from 0 to the prompt's tokens, `delayMs` apart - only when the request has `"return_progress": true`, as llama.cpp |
| `{"cut": "now"}` / `{"cut": "before-headers"}` | reset the TCP connection (RST) |
| `{"malformed": true}` or `{"malformed": "raw"}` | one `data:` line that is not JSON |
| `{"raw": "..."}` | bytes written as is |
| `{"finish": "length"}` | empty delta with that `finish_reason` (any string, or null) |
| `{"usage": true}` | usage chunk (`choices: []`) with prompt/completion tokens and llama.cpp `timings` |
| `{"done": true}` | `data: [DONE]` |
| `{"error": {"status", "body", "headers"}}` | before headers: that HTTP error; after headers: an in-stream `data: {"error": ...}` |

An `error` or `cut` as the very first step happens before any header. When the steps run out the body ends cleanly (chunked terminator) - without a `done` step that is the "EOF without [DONE]" case. Placeholders in any string: `{n_ctx}` (slot context), `{prompt_tokens}`, `{request}` (server-wide request number), `{model}`; a string that is exactly one placeholder becomes that value with its type (e.g. an integer `n_ctx`).

`wire` keys checked by `probe.py`: `transport` (`done` = saw `[DONE]`, `eof` = clean end without it, `reset`, `http_error`, `no_response`), `status`, `finishReason`, `text`, `textIncludes`, `reasoningIncludes`, `errorIncludes`, `toolCalls` (`[{name, argumentsValid}]` after merging deltas by id, then index), `malformed`, `comments` (minimum), `minSilence` / `maxSilence` (longest gap, in unscaled seconds).

## Scripts by stop class (`docs/12` taxonomy)

| Stop class | Script | Responses | What the wire does |
|---|---|---|---|
| `connect_failed` | `connect-failed` | 2 | TCP accepted, then reset before any HTTP response; the 2nd request succeeds. (A refused port is the other shape: point at a closed port.) |
| `http_error` | `http-error-503` | 3 | 503, 503, then success (backoff, fallback after 2) |
| `http_error` | `http-error-429-retry-after` | 2 | 429 with `Retry-After: 2`, then success |
| `disconnected_mid_stream` | `disconnect-mid-stream` | 2 | partial text, then RST; the 2nd request continues |
| `disconnected_mid_stream` | `eof-without-done` | 2 | partial text, clean end of body, no finish_reason, no `[DONE]` |
| `stalled_prefill` | `stall-prefill` | 2 | no bytes at all (not even headers) for 120 s |
| `stalled_prefill` | `stall-prefill-after-headers` | 2 | headers at once, then 120 s without a chunk |
| `stalled_prefill` | `stall-prefill-twice` | 2 | 150 s of silence, cancelled; then 140 s, which the retry's longer deadline outlasts |
| none | `stall-prefill-after-headers` + `metrics: "busy"` | - | the same silence, and the watchdog's question answered: the request is left to finish, nothing is retried (docs/12 "Watchdogs") |
| `stalled_stream` | `stall-mid-stream` | 2 | ~200 tokens, 120 s of silence, then the rest |
| `max_tokens` | `max-tokens` | 2 | text cut by `finish_reason=length`; the 2nd request finishes |
| `max_tokens` | `max-tokens-mid-tool-args` | 2 | tool-call arguments cut by `length` (invalid JSON); the 2nd request sends the full call |
| `empty` | `empty` | 1 | `finish_reason=stop`, no content, no tool calls |
| `repetition` | `repetition-loop` | 2 | reasoning that repeats one sentence ~190 times (cut and discarded once its tail is one passage repeated); the 2nd request answers |
| `reasoning_only` | `reasoning-only` | 1 | only `reasoning_content`, then stop |
| `reasoning_only` | `reasoning-only-think-tag` | 1 | content that is only an unclosed `<think>` |
| `announced_no_action` | `announced-no-action` | 1 | "Now I'll edit src/game/player.c to fix it:" and stop |
| `unparsed_tool_call` | `text-tool-hermes`, `text-tool-qwen`, `text-tool-mistral`, `text-tool-mistral-args`, `text-tool-fenced-json`, `text-tool-harmony` | 1 each | the tool call as text in each dialect |
| `malformed_tool_call` | `malformed-tool-args` | 2 | native call with invalid JSON arguments; the 2nd request is valid |
| `unknown_finish` | `unknown-finish-eos`, `unknown-finish-end-turn` | 1 each | complete answers ending with `eos` / `end_turn` |
| `context_overflow` | `context-overflow-llamacpp` | 2 | HTTP 400 `exceed_context_size_error` with `n_ctx` (= the configured slot context) |
| `context_overflow` | `context-overflow-openai` | 2 | HTTP 400 `context_length_exceeded`, the limit only in the message text |
| `context_overflow` | `context-overflow-in-stream` | 1 | HTTP 200, then an in-stream error object |
| `loop_detected` | `loop-same-call` | 1 | the same `read_file` on a missing file, every request |
| `no_progress` | `no-progress` | 1 | text without a tool call or finish, every request |
| `budget` | `budget-runaway` | 1 | a different `read_file` every request (`src/file_{request}.c`) |
| `user_cancelled` | `long-stream` | 1 | ~1800 chunks 50 ms apart, to press Stop on |
| `none` (baseline) | `ok-text`, `ok-tool-call`, `ok-reasoning-then-answer` | 1 each | normal answers |
| `none` (must not stop) | `slow-prefill-keepalive` | 1 | 90 s prefill with a keepalive comment every 10 s |
| `none` (must not stop) | `malformed-chunk` | 1 | one bad `data:` line mid-stream; skip it and go on |
| `none` (must not stop) | `done-without-finish` | 1 | `[DONE]` with no finish_reason ever |
| `none` (must not stop) | `tool-calls-same-index-different-ids` | 1 | two calls at index 0 with different ids: merge by id gives two calls (the predecessor IDE's bug) |
| `none` (must not stop) | `tool-call-id-on-every-delta`, `tool-calls-without-ids` | 1 each | id/name repeated on every delta; index-only parallel calls |
| `none` (must not stop) | `reasoning-field-variants` | 1 | reasoning in `reasoning` and `thinking` fields |

Each script's `harness` field states the expected IED recovery and its bound. The agent-level classes (`loop_detected`, `no_progress`, `budget`) only show up across many requests, which is why those scripts repeat their last response. `restart mid-tool` (`docs/19`) is a server concern, covered by the mock server's `testhooks.restart`, not by this fixture.

## Files

| File | Role |
|---|---|
| `fake_model.py` | HTTP server, script selection, discovery endpoints, control API |
| `streaming.py` | runs one response: SSE sink, non-stream collector, the step ops |
| `scripting.py` | script loading, validation (unknown keys are errors), placeholders |
| `dialects.py` | text renderings of a tool call per dialect |
| `probe.py` | client: timeline, transport classification, merge-by-id-then-index reference, `wire` checks |
| `scripts/*.json` | one file per script |
| `tests/` | every script against its `wire` block, every stop class covered, endpoints, queue, slots, context enforcement, README coverage |
