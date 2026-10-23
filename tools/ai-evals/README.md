# ai-evals - how a real model uses IED's tools

Checks with the fake model (`tools/server-checks`) prove the tools work; these runs show whether a real model chooses
them and uses them well, so prompts, tool descriptions and error messages can be tuned from what it actually does.

## lang/ - whole tasks in a language

`lang/run.py` gives a model whole programming tasks in IED's Agent mode - from-scratch programs, features, bugs,
refactors, optimizations, long ports, tests - in C4 and, to compare, in Go, and measures tokens, time, tool calls,
the result's quality (hidden tests, program checks, mutants) and its runtime (time and memory): `lang/README.md`.

## codenav_eval.py - navigating code (`docs/13` "Navigating code")

Eight questions about the IED server's own sources (which function picks a document's language server, a class's
members, the callers of a method, what changing a function's parameters touches, a request traced through the
server, ...), each in its own read-only Ask chat of a server started for the run, against a model of your app settings
(`ai.models` in `%APPDATA%\IED\settings.json`, read at run time - its address never leaves that file). The workspace is
the server's sources as their own Nuke workspace, so C4LSP has the project's description.

```bat
vendor\python\python.exe -I tools/ai-evals/codenav_eval.py --server build\bin\Development-windows-x64\ied-server\ied-server.exe --setup
vendor\python\python.exe -I tools/ai-evals/codenav_eval.py --server <exe> --model qwen3.8-27b@lan --only members,trace
```

- `--setup` (first run, or after the sources change) copies `src/` and the plugin code its `nuke.toml` names into
  `build/ai-evals/Srv`, runs `nuke init --ied` and `nuke build ied-server`.
- Results land in `build/ai-evals/<time>/`: `report.json` (each turn's calls, answer, tokens, time, each step's stats)
  and `summary.md`: per question its quality (the facts a right answer states, found in it: the functions, files and
  constants it must name), time, steps (requests to the model), calls, prompt and output tokens, thinking and answer
  size, whether it used the tool the question calls for and its misuses (a text search for an identifier, a regex sent
  as literal text, a whole source file read, failed calls); then the time split into prompt processing, generation and
  the rest (tools, the server), each tool's calls and result sizes, and each turn's calls in order.
- `--temperature` (default 0) makes runs repeatable enough to compare one change with the last; a change is kept when
  quality holds and time, steps or tokens fall.
- Use the repository's Python (`vendor\python`): a Microsoft Store Python reads its own private copy of AppData and does
  not see the app settings.
- `--effort` (`low`, `medium`, `high`, `xhigh`, `max`) is the eval role's effort (`docs/04` "Model groups and roles"); left out, the
  model server's default (Qwen3.8's template: its longest, `xhigh`).
- `--parallel` (default 1) is how many turns run at once. More than one makes them share the model server's GPU: they
  finish sooner together but each takes several times longer, so their times do not compare with a sequential run.

The fact checks catch an answer that misses what matters, not one that is wrong around it; read the answers in
`report.json` when a number moves.

### Runs

| Date | Model | Change | Result |
|---|---|---|---|
| 2026-10-21 | Qwen3.8 27B (Q4, llama.cpp) | baseline: core `code_symbols`, `code_read_symbol`, `code_definition`, `code_references`; the rest deferred | 147 calls, 39 misuses, 2 questions without their tool. Code tools chosen on their own in every turn and every answer correct, but the deferred tools were never loaded: `members` read the header instead of `code_members`, `trace` took 54 calls without `code_call_hierarchy`; text searches for identifiers after a code tool had found them, regexes sent as literal text, `code_read_symbol` with only a path; `code_references` found nothing from a declaration (a C4LSP bug, reported) |
| 2026-10-21 | same | `code_members`, `code_call_hierarchy` core; "Navigating code" in the system prompt; `code_symbols` by a name's words; `search_text` retries a regex-looking literal; empty results teach | 133 calls, 19 misuses: `members` used `code_members`, `callers` started with the call hierarchy, `paste` went from 28 calls (10 text searches) to 13 (none); `trace` still 60 calls without the hierarchy |
| 2026-10-21 | same | the prompt's flow line (outgoing tree first); absolute paths of files outside the workspace taken | 99 calls, 8 misuses; `trace` 28 calls. One answer wrong: `callers` trusted an incomplete call hierarchy (C4LSP missed calls through `->`) and named 2 of 4 - so `code_references` and incoming `code_call_hierarchy` now list the lines naming the symbol that the server did not report |
| 2026-10-22 | same | C4LSP fixed (references from a declaration, calls through `->`; a1b172d); the unlisted-lines note | `callers` right in 2 calls (4 of 4), `change` complete in 7; text search nearly gone. `trace` fell back to reading whole files (16) and hit two bugs: `fs_read` refused the absolute path the code tools show for a library file, `code_symbols` on a folder said "no language server serves" - both fixed |
| 2026-10-22 | same | `fs_read` takes those absolute paths; a folder for `code_symbols` is said to be one | rerun of `trace`, `members`, `errors`: `trace` used `code_call_hierarchy` (3 times), 40 of 47 calls code tools, 5 misuses (was 15); `members` and `errors` 2 each |
| 2026-10-22 | same, temperature 0, one turn at a time | baseline for time and tokens (the runs above ran 4 turns at once, so their times do not compare); quality now scored by fact checks | 38/38 facts, 853 s, 77 steps, 153 calls, 18.8k tokens out; 670 s of the 853 generating, so steps and output are what cost time |
| 2026-10-22 | same | `code_symbols` gives its only match's source when short; its word search keeps the best 25; `code_members` without the owner's qualification; `search_text` grouped by file, long lines cut; unlisted lines only uses as code, a common name's counted; prompt: independent calls together, a plain name read directly, short quotes | 38/38, 769 s (-10%), 65 steps (-16%), 115 calls (-25%), 17.6k out; no symbol was read again after a search gave its source. `trace` spent 9 of 22 steps finding where `LspPool` is made: C4LSP gives neither a constructor's `new T(...)` nor a class's uses in other files (reported) |
| 2026-10-22 | same | prompt: no closing summary, no side notes | 38/38, 731 s, 59 steps, 117 calls, 16.9k out; answers barely shorter (17.8k chars): the gain is mostly run-to-run change. Half the output is the final reply (its thinking and the answer), half the tool steps (~160 tokens each, mostly thinking); `request` read things the question did not ask about |
| 2026-10-22 | same | prompt: stop once the question can be answered | 38/38, 730 s, 56 steps, 109 calls; the seven questions besides `trace` 475 -> 444 s. `trace` still spends its steps looking for where `LspPool` is made |
| 2026-10-22 | same | `--effort low` (IED now sends a role's effort; a llama.cpp server gets the template's `reasoning_effort`) | 38/38, 458 s (-37% from the unset run above), 40 steps, 51 calls, 10.0k out, thinking 11.9k chars (was 27.9k). Fewer steps because it reads whole files more (`trace` read 5; prompts to 30k tokens), which the prompt cache makes cheap here but would not scale to big files |
| 2026-10-22 | same | `--effort medium` | 38/38, 494 s (-32%), 56 steps, 88 calls, 10.6k out, thinking 9.7k chars: this template gives medium no instruction, low a "keep it brief" one; it kept reading by symbol. Recommended for this model: medium |
| 2026-10-22 | same | `--effort medium` again, with C4LSP's references fixed (a472b79: constructions, a class's uses in other files) | 38/38, 496 s, 55 steps: the same run but for `trace`, whose `code_references LspPool::Init` now names `new LspPool(self)` and so needs no text search after it |
