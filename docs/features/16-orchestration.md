# Features: orchestration and mission control (ORC)

Owner: `src/ai` (scheduler, queue, verifier) + `plugins/chat` (Mission Control UI). Design: `docs/12` (runs, sub-agents), `docs/04` (routing).

This is the area where IED goes furthest beyond its predecessor. The predecessor IDE had one model per chat plus read-only research helpers; its Kanban/orchestrator/heartbeat/self-healing designs were never built (their docs drifted from the code). IED makes **many agents doing real work in parallel, unattended, verified, and steerable from a phone** a core capability, built on the server-owns-everything architecture.

## Mission Control

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-1 | Mission Control activity (and phone home screen): every run on the server across workspaces and generic chats - status, mode, model(s), task progress, current step/tool, elapsed, tokens/cost, attention flags; inline approve/answer/pause/resume/stop | P0 (list + actions), P1 (full) | US-ORC-1 |
| F-ORC-2 | Attention queue: one ordered list of everything waiting on the owner (approvals, questions, failed verifications, merge reviews), with keyboard and swipe triage | P0 | US-ORC-1 |
| F-ORC-3 | Live resource view: per model endpoint - slots busy/free, queue depth, tokens/s, which runs hold which slot; per local box GPU/VRAM when the endpoint reports it | P1 | US-ORC-5 |

## Goals, queue and board

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-10 | Goals: a unit of work with title, description, acceptance criteria, workspace (or federation member), mode, model group, isolation, budgets, priority, dependencies | P1 | US-ORC-2 |
| F-ORC-11 | Goal queue per workspace: the scheduler starts the next ready goal when capacity (model slots, concurrency limit) frees up; owner can pin order, pause the queue, set quiet hours | P1 | US-ORC-2 |
| F-ORC-12 | Board view: Backlog / Ready / Running / Review / Done columns; drag to reorder or move; each card links to its chat, branch/worktree, diff and report | P1 | US-ORC-2 |
| F-ORC-13 | Goal capture everywhere: "Make this a goal" from a chat message, a TODO in code, an Error List row, a failed test, a profiler hot spot; the AI may *propose* goals (they land in Backlog, never start on their own) | P1 | US-ORC-2 |
| F-ORC-14 | Plan-to-goals: a Plan-mode result can be split into dependent goals in one click (milestones -> goals, tasks -> goal tasks) | P1 | US-ORC-2, US-AGT-3 |

## Isolation and review

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-20 | Worktree isolation: a goal can run in its own git worktree + branch (`ied/<goal-slug>`), so parallel agents never edit the same checkout; build outputs per worktree | P1 | US-ORC-3, US-GIT-6 |
| F-ORC-21 | Review stage: finished goals land in Review with the diff (phone-friendly), verification evidence and report; actions: merge (ff / squash / merge commit), request changes (text goes back to the agent as steering and the goal resumes), discard | P1 | US-ORC-3 |
| F-ORC-22 | Conflict awareness: the board warns when two goals touch the same files; merge order suggestions; auto-rebase of a reviewed branch onto the target before merge (with re-verification) | P2 | US-ORC-3 |
| F-ORC-23 | Sandbox isolation: a goal can run with `target: sandbox` so its commands execute in a container (and its network is deny-by-default) | P2 | US-ORC-3, US-SBX-1 |

## Verification

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-30 | Acceptance criteria are executable where possible: tasks to run (build, tests, a script) and expected outcomes; `finish` must cite them | P1 | US-ORC-4 |
| F-ORC-31 | Independent verifier: after `finish`, a verifier agent (role `verifier`: ideally a *different* model, temperature 0) re-runs the acceptance tasks, reads the diff and the claims, and returns pass/fail with findings; fail -> goal reopens with the findings as steering (bounded attempts, then Review with "verification failed") | P1 | US-ORC-4 |
| F-ORC-32 | Evidence bundle per goal: commands run with exit codes, test results, screenshots/image diffs (for visual work), profiler summaries - attached to the report | P1 | US-ORC-4 |

## Scheduling and triggers

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-40 | Scheduled goals: cron-style schedules (e.g. "02:00 nightly: run all tests; if anything fails create and run a fix goal") | P1 | US-ORC-6 |
| F-ORC-41 | Event triggers: build failed, tests failed on main, file pattern changed, git pull brought new commits, a plugin event (bindings) -> create a goal from a template | P2 | US-ORC-6 |
| F-ORC-42 | Away mode: while away, approval-gated actions in one goal do not block the queue (the goal waits in Attention, others proceed); only blockers notify the phone; a digest summarizes everything on return | P1 | US-ORC-7 |
| F-ORC-43 | Interactive priority: requests from a chat the owner is actively using preempt background goals on the same model (background requests yield slots at the next step boundary) | P1 | US-ORC-5 |
| F-ORC-44 | Daily/weekly caps for cloud spend and per-queue token budgets | P1 | US-ORC-6 |

## Cross-workspace work

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-50 | Federation goals: one goal with ordered phases in different members ("fix compiler bug in C4 -> refresh vendored C4.exe in the game project -> rebuild the game -> rerun its tests") | P1 | US-ORC-8 |
| F-ORC-51 | Handoff: an agent can create a goal in another workspace's queue with a repro and context (e.g. a game-project agent that finds a C4 compiler bug files it in the C4 workspace, per the family rule "a compiler bug is a compiler fix, not a workaround"), and optionally wait for it | P1 | US-ORC-8 |

## Model quality loop

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-60 | Model scorecards from real usage, per model x role: tool-call validity, stall/premature-stop rates, recovery success, verification pass rate, tokens/s, prefill rate, cost, average steps per finished goal | P1 | US-ORC-9 |
| F-ORC-61 | Scorecard-informed routing (opt-in): among eligible models, prefer higher verification pass rate for `implementation`, higher tokens/s for `worker`, etc. | P2 | US-ORC-9 |
| F-ORC-62 | Eval harness ("Arena"): run a suite of recorded or authored tasks (`.ied/evals/*.json`: setup, prompt, acceptance tasks) against several model groups in parallel worktrees/sandboxes; compare pass rate, time, tokens, cost; export results | P1 | US-ORC-9 |
| F-ORC-63 | Regression guard for the harness itself: the eval suite runs against the fake model server's failure scripts on every IED build (`docs/19`) | P1 | US-AGT-1 |

## Learning loop

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-ORC-70 | After a goal succeeds, the runtime proposes lessons: new/updated skills, `AGENTS.md` amendments, dictionary words - as reviewable diffs in Review, never auto-applied | P1 | US-CTX-6 |
| F-ORC-71 | Failure post-mortems: a goal that failed verification twice gets an automatic short analysis (what went wrong, which model, which stop reasons) attached to its card and fed into scorecards | P2 | US-ORC-9 |
