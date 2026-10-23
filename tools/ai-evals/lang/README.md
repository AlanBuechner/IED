# lang - how well a real model writes a language through IED

`codenav_eval.py` (one folder up) asks questions about code; this eval gives a local model whole programming tasks in
IED's Agent mode - new projects, features in existing ones, bugs, refactors, optimizations, long ports - in C4 and,
for comparison, in Go, and measures what it costs and what it produces. Its purpose is to tune what IED gives the
model for a language (tools, system-message sections, skills, docs) from what the model actually does, without
over-fitting a few tasks.

```bat
rem check the tasks themselves (every reference solution must score 1.0, every untouched start low)
vendor\python\python.exe -I tools/ai-evals/lang/selftest.py
rem the tuning tasks in C4, three at a time, with the baseline variant
vendor\python\python.exe -I tools/ai-evals/lang/run.py --server <ied-server.exe> --set tune --langs c4 --parallel 3
rem one task in both languages with a variant
vendor\python\python.exe -I tools/ai-evals/lang/run.py --server <exe> --tasks spans --langs c4,go --variant skills2
rem tables
vendor\python\python.exe -I tools/ai-evals/lang/report.py <run>
vendor\python\python.exe -I tools/ai-evals/lang/report.py --compare <run> <run> ...
```

Use the repository's Python (`vendor\python`): a Microsoft Store Python sees a private copy of AppData without the
model settings. The model is a profile of the app settings (`ai.models` in `%APPDATA%\IED\settings.json`); its
address never leaves that file - the eval's servers talk to a recording proxy on localhost (`proxy.py`).

## How a run works (`run.py`)

For each task and language: the task's starting project (`tasks/<id>/<lang>/fixture/`, or a fresh `nuke init` /
`go mod init` for a from-scratch task) is copied to `%TEMP%\ied-langeval\runs\<run>\<task>-<lang>\ws`, committed
to a throwaway git repository (so the grader sees what changed), and opened in an ied-server of its own (C4: the
solution `nuke init --ied` writes, which brings the toolchain's C4 plugin and its skills; Go: the folder). The task's
prompt goes to a new Agent-mode chat with permission `auto`; approvals are granted and `ask_user` questions answered
"make the reasonable choice" so the run never waits on a person. The turn runs until `finish`, a pause, or the task's
time limit. Then `grade.py` grades the workspace.

- `--temperature` defaults to 0.6: Qwen3 asks for it in thinking mode, and at 0 it fell into a reply that repeated
  itself for 30 minutes (now cut by the agent runtime, `docs/12` `repetition`).
- `--parallel N` runs N tasks at once on the model server; wall times then include waiting for the GPU, so compare
  **solo seconds** (below), not wall seconds, across runs with different parallelism. A task's `timeout_minutes` is
  a budget, not a wall-clock limit: the turn stops once its solo seconds reach 0.45 of it (what a job got of it with
  four jobs on the server, measured), so a busy server never shortens a job; twice it in wall seconds stops it anyway.
- `--repeat N` runs everything N times: at temperature 0.6 one run of a task is a sample, not a measurement.
- `--slots N` (default 4, the owner's llama.cpp server): runs started side by side share N lock files
  (`slots.py`), so they never send the server more turns at once than it has slots - one more would wait in its
  queue, and IED's prefill watchdog would take the wait for a stall.

## Variants

A variant (`variants/<name>/`) is what the model is given, changed without touching the tree: `home/` files go over
a copy of IED's `plugins/`, `frontend/` and `shared/` (the chat's prompts, other plugins' manifests and skills), and
`toolchain/` files over a copy of the dev C4 toolchain (`ied/`: the C4 plugin's skills and system-message section;
`docs/`). `baseline` is empty. A `variant.json` may say what the variant is (`description`) and copy folders within
the toolchain copy (`toolchainCopies`: `[["docs", "ied/docs"]]`), after writing any API digest it asks for
(`apiDigests`: `[{"library": "Bedrock", "out": "docs/library/bedrock-api.md", "title": "..."}]`, by `api_digest.py` from
the run's own toolchain, so it always matches the headers). `starter/` files go over what `nuke init` writes for
a C4 task that starts from nothing (`{name}` becomes the project's name). Each run prepares its own copies under
`%TEMP%/ied-langeval/{toolchains,homes}/<run>`, so two runs of one variant never rewrite each other's files. Changes to
tools (C4 code under `plugins/chat/code/`) need a server build: pass that build's exe with `--server`, and with it
`--home-base`, a snapshot of the tree's home from the commit it was built at (`git archive <commit> plugins frontend
shared`): the home otherwise comes from the tree, whose plugins may since need code the build does not have (a chat
manifest newer than the build left the chat without its methods). A variant's `home-merge/` JSON files are merged
into that home (a schema the variant adds to) rather than copied over it.

## Tasks (`tasks/<id>/task.toml`)

Each task has a C4 and a Go version that behave identically, a `set` (`tune`: iterated on; `holdout`: run only to
check that a change generalizes), and a prompt written as a user would.

A task has to sit in the sweet spot of what it tests: hard enough that setups differ, doable enough that a good one
succeeds. After each round, look at every task's spread across setups and languages (scores, finished jobs, the
review's correctness); a task every setup aces or every setup fails tells nothing and is recalibrated as a new task
(`<id>2`, `version = 2` saying what changed and why) in the next round's sets, so a round's runs always compare on the
same tasks. Round 2 (`tune2`, `holdout2`; unchanged tasks are in both rounds, `set = ["tune", "tune2"]`):
`bugs2` has six bugs, two in parsing and grouping (every setup fixed round 1's four); `techdebt2` has no median or
honor roll and ten more minutes (no setup got through round 1's refactor); `spans2` drops the O(log n) demand (every
implementation broke in the splicing it needs; `optimize` measures speed work); `explain2` asks a third question
(every setup stated round 1's facts). Requirements score their progress from the starting project, and a refactor's
or a speedup's checks count only as far as it went (`requirements.gate`), so an untouched program scores about 0.
What is graded:

| | |
|---|---|
| `[[checks]]` | runs of the program: arguments (`{data}` is the task's `data/`), stdin, exit code, exact stdout (or a file's), text in stderr, JSON compared by value |
| `<lang>/hidden/` | acceptance tests copied in after the turn (C4 `[test("hidden...")]`, Go `TestHidden*`), against an interface the prompt fixes |
| `[bench]` | a generated input (`data/gen.py <out>` writes it and the expected output); the program's median time, CPU and peak memory over 3 runs |
| `[requirements]` | demands on the code: `globals_max`, `function_lines_max`, `own_tests_min`, `speedup_min` (over the untouched start) |
| `[[mutants]]` | for a test-writing task: one-place changes of the code the agent's tests must catch |
| `answer_facts` | for a question: regexes a right answer matches |
| `[idioms.<lang>]` | patterns that show the language used well or badly (reported, not scored) |

`fixture_files` (`[[from, to]]`) copies files of the task's folder into both starting projects, so big inputs are
stored once. `<lang>/reference/` is a correct solution (files over the start). `selftest.py` grades it and the untouched start,
and records both benchmark times in `references.json`, which the grader uses for speedups and for "time and memory
against the reference".

| Task | Set | Kind | What it asks |
|---|---|---|---|
| `wordfreq` | tune | from scratch | a word-frequency CLI, fast on 60 MB |
| `spans` | tune | from scratch, fixed API | a set of integer spans with hidden tests, and a command-file driver |
| `ledger` | tune | feature | options on every command and a report in a multi-file program |
| `bugs` | tune | debugging | four bugs reported as symptoms |
| `optimize` | tune | performance | a naive log analyzer 10x faster, output unchanged |
| `techdebt` | tune | refactor | one 150-line main with globals made clean, behavior unchanged |
| `matrix` | tune | from scratch, fixed API | a matrix class with operators (C4: `[operator]` methods), inverse and determinant, hidden tests |
| `interp` | holdout | large project | while loops, arrays and two builtins added to a 1750-line interpreter (lexer, Pratt parser, closures) |
| `explain` | holdout | questions | how that interpreter parses precedence and binds a call's arguments, answered from the code (Ask mode) |
| `jsonagg` | holdout | standard library | a JSON order report (reading, aggregating, writing JSON) from scratch, with a 47 MB benchmark |
| `mutants` | holdout | tests | unit tests for a working semver library, graded by 25 mutants |
| `port` | holdout | long run | a 770-line C++ Markdown-to-HTML converter ported with its tests, in a 32k context (compression) |

## Metrics (`report.py`)

- **Quality:** `score` in [0, 1]: hidden tests 0.45, program checks 0.35, a warning-free build 0.1, passing tests of
  its own 0.1, plus mutants killed and requirements met where the task has them. A turn that called `finish` with a
  score under 0.8 is a **false success**. Code: lines, functions over 40 lines, globals, idiom counts; a blind review
  of the code by a stronger model is a separate step.
- **Tokens:** from the model server itself (`model.jsonl` of the proxy, llama.cpp `timings`): prompt tokens read
  (`prefill`, cache reuse left out), all prompt tokens sent, generated tokens (thinking included), the peak context.
- **Time:** wall seconds; **solo seconds** = prompt tokens read / 900 + generated tokens / 30 + time in tools: what
  the turn would take alone on the owner's model server.
- **Tool calls:** steps (model requests), calls, failed calls, repeated identical calls, builds and failed builds,
  calls by kind (navigation, reading, docs, skills, edits, builds, shell).
- **Runtime:** the benchmark's time and peak private memory, and both against the reference solution.

Results: `build/ai-evals/lang/<run>/` - `run.json`, `summary.md`, `rows.json`, and per job `agent.json` (the turn),
`grade.json` (each program check with its time and peak working set), `transcript.jsonl` (the turn as IED stored it:
tool calls with their result previews), `model.jsonl` (each model request's size and timings), `first-request.json`
(the system message and tool definitions it saw) and `last-request.json` (the whole conversation as last sent, every
tool result in full unless compression dropped it).
