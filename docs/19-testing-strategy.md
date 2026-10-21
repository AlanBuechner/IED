# 19 - Testing strategy

## Purpose

Define how IED is verified, so "done" (AGENTS.md) is mechanical, not a feeling. Every milestone in `docs/18` has gates from this doc. Owner rules that shape it: test interactions not just code paths; screenshot-validate UI at desktop and phone widths; verify the resume path; a failing test is fixed, never deleted or weakened.

## Test layers

| Layer | What | Runner | Where |
|---|---|---|---|
| **Unit** | Honest functions in the server (protocol codecs, OT rebase, matchers, `when` evaluator, variable expansion, routing decisions, compression planning, stop classification, ITP decoding) | C4 `[test]` via `nuke --test` | `src/**/tests/` |
| **Contract** | Every on-disk format and every frame validates against `shared/host-api` schemas; examples stay valid; schema changes need a version bump | schema validator (Python, `tools/`) | `shared/host-api/` |
| **Protocol conformance** | A black-box suite that drives a server over WS: hello/welcome, req/res/errors, sub/snap/evt ordering, resume from cursor, gap -> resync, idempotent `opId`, backpressure, cancellation, doc lease + rebase | Python (`tools/conformance/`) | runs against the **mock server** and the **real server** - both must pass the same suite |
| **Plugin host** | Manifest validation errors, permission enforcement, binding resolution, plugin C4 code (compile errors in the Error List, faults that fail only the plugin, hot reload, the compiled-script cache, a server without its script library), trust gating | `tools/server-checks/tests/test_plugin_code.py`, `test_plugins.py` (plugins written by each check) | |
| **Harness (AI runtime)** | Every stop class in `docs/12` has a scripted regression: stall in prefill, stall mid-stream, `length` mid-tool-args, malformed chunk, socket close, text tool calls (each dialect), unknown finish reason, context error with limit, empty and reasoning-only replies, announced-no-action, loops, budget hits, restart mid-tool | **fake model server** (Python, OpenAI-compatible, scriptable per request) | `tools/fake-model/` |
| **Frontend unit** | Layout tree ops, keybinding resolution (chords, `when`), ANSI parser, virtualized lists, WS client resume logic | in-browser test page run headless by Playwright | `frontend/tests/` |
| **E2E** | Journeys J1-J14 (automatable parts), plus per-feature interaction scripts | Playwright (Python) at 3 viewports: 1920x1080, 1366x768, 390x844 | `tests/e2e/` |
| **Visual** | Golden screenshots of key screens and states (empty, loading, error, populated) per viewport; pixel diff with a small threshold; reviewed when they change | Playwright screenshots + `image_compare` | `tests/visual/` |
| **Soak / perf** | 24 h server soak with fake models and synthetic builds; flood tests (1M-line build output, 10 concurrent streams, 50 terminals); big repo (open a federation of this repo with C4 plus a game project); latency budgets | scripts + server health endpoint | `tests/perf/` |
| **Security** | Host-header/DNS-rebinding, Origin, CSRF, token reuse/expiry, login rate limit, path traversal and symlink escape in fs tools, secrets redaction in logs/trajectories/tool results, untrusted-workspace gating | Python | `tests/security/` |
| **Agent quality** | Eval suite (F-ORC-62): fixed tasks with executable acceptance criteria, run against model groups; tracks pass rate per IED build to catch harness/prompt regressions | IED's own eval harness (after M7); before that, a script driving the API | `.ied/evals/` |

**As built (M1).** `nuke e2e` lints the web code (ASCII, tabs, no raw colors outside `theme.css`, across `frontend/`, `shared/ui-kit/` and `plugins/`), runs the mock's own tests, then `tests/e2e/run_e2e.py`: the in-browser unit tests (`frontend/tests/`: when-clauses, keys and chords, the layout tree, ANSI, the protocol client against a fake socket and clock, document edits), then every shell scenario at 1920x1080, 1366x768 and 390x844 (a tablet check besides) against the mock and the real server - docking with persistence, menus and the palette, tabs and their menu, notifications, Output streaming, trust, plugins, the phone's panes, and the resume paths (a server restart; a page closed mid-stream and reopened). Screenshots land in `build/e2e/<server>-<viewport>/`. It needs Playwright for Python (`python -m pip install playwright`, `python -m playwright install chromium`); when Nuke's bundled Python has none, the runner uses a Python on PATH that does.

## Test doubles that unblock work before the compiler is ready

1. **Mock server** (`tools/mock-server/`, Python): implements protocol v1 with in-memory fixture data (a fake workspace tree, documents, a scripted chat stream, a build run with ANSI output, a terminal echo). It lets the whole frontend be built, screenshot-tested and resume-tested in M0. The conformance suite keeps it honest.
2. **Fake model server** (`tools/fake-model/`, Python): OpenAI-compatible SSE with a script language (`emit text`, `emit tool_call(name,args)`, `emit text_tool_call(dialect)`, `stall 90s`, `cut`, `finish(reason)`, `error(status, body)`). Used by harness tests now (against the mock server's simulated runtime) and against the real runtime later.
3. **Real family binaries**: C4LSP and C4DAP exist today (sources in `C4/C4LSP/` and `C4/C4DAP/`, binaries shipped through Nuke's `tools/`), so LSP/DAP client tests run against the real servers from M2 on, not mocks.

Python is already in the family toolchain (Nuke vendors it), so these add no new language to the stack.

## Resume-path tests (required for every streaming feature)

| Scenario | Expectation |
|---|---|
| Browser killed mid-stream, reopened within ring retention | stream continues; missed events replayed exactly once; no duplicate tokens |
| Browser reopened after retention expired | snapshot restores completed state; live tail continues |
| Network flap (proxy drops 30% of frames for 60 s) | `gap` -> resync; UI never shows corrupted text |
| Server restarted mid-run | new `sessionEpoch`; chats/docs/layout restored from disk; run shows `interrupted` and resumes per policy; in-flight tool calls not re-executed |
| Phone and desktop both attached | approvals answered once; doc lease visible; layouts independent |

## Interaction and screenshot protocol (per UI feature)

1. Script the interaction (Playwright) at desktop and phone widths.
2. Capture screenshots at each meaningful state (before, during, after, error).
3. Compare against goldens; new or changed goldens are reviewed by looking at them (the implementing agent attaches them to its report).
4. Check keyboard path and context-menu path exist (`docs/15` rule).
5. Check the error path renders an actionable message.

## Performance budgets (initial; tune with data)

| Metric | Budget |
|---|---|
| Keystroke to render in editor (local) | < 16 ms p95 |
| Server doc edit round trip (LAN) | < 30 ms p95 |
| Completion request to list shown (C4LSP warm) | < 150 ms p95 plus server time |
| WS event latency server -> client (LAN) | < 20 ms p95 at 1k events/s |
| Open a 5k-file solution to interactive tree | < 2 s |
| Find in files over 50k files | first results < 300 ms (ripgrep) |
| Build output flood | 50k lines/s ingested without UI jank (coalescing) |
| Server idle memory | < 300 MB with one workspace, no captures |
| Profiler timeline pan/zoom | 60 fps on a 1 GB capture (server LOD) |

## Gates

A change is mergeable when: unit + contract + conformance + harness suites pass; affected e2e/visual tests pass at all three viewports; no new warnings in the server build (`nuke build` warning-free, matching the family rule); docs and skills updated to match behavior. Milestone exits additionally require their journeys (`docs/18`).
