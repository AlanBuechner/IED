# ai-evals - how a real model uses IED's tools

Checks with the fake model (`tools/server-checks`) prove the tools work; these runs show whether a real model chooses
them and uses them well, so prompts, tool descriptions and error messages can be tuned from what it actually does.

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
- Results land in `build/ai-evals/<time>/`: `report.json` (each turn's calls, answer, tokens, time) and `summary.md`: per
  question its time, calls, `code_*` / `search_text` / `fs_read` counts, whether it used the tool the question calls for,
  and its misuses (a text search for an identifier, a regex sent as literal text, a whole source file read, failed
  calls), then each turn's calls in order.
- Use the repository's Python (`vendor\python`): a Microsoft Store Python reads its own private copy of AppData and does
  not see the app settings.
- `--parallel` (default 4) is how many turns run at once; match the model server's slots.

The answers are not graded automatically; read them in `report.json` when a route looks wrong.

### Runs

| Date | Model | Change | Result |
|---|---|---|---|
| 2026-10-21 | Qwen3.8 27B (Q4, llama.cpp) | baseline: core `code_symbols`, `code_read_symbol`, `code_definition`, `code_references`; the rest deferred | 147 calls, 39 misuses, 2 questions without their tool. Code tools chosen on their own in every turn and every answer correct, but the deferred tools were never loaded: `members` read the header instead of `code_members`, `trace` took 54 calls without `code_call_hierarchy`; text searches for identifiers after a code tool had found them, regexes sent as literal text, `code_read_symbol` with only a path; `code_references` found nothing from a declaration (a C4LSP bug, reported) |
| 2026-10-21 | same | `code_members`, `code_call_hierarchy` core; "Navigating code" in the system prompt; `code_symbols` by a name's words; `search_text` retries a regex-looking literal; empty results teach | 133 calls, 19 misuses: `members` used `code_members`, `callers` started with the call hierarchy, `paste` went from 28 calls (10 text searches) to 13 (none); `trace` still 60 calls without the hierarchy |
| 2026-10-21 | same | the prompt's flow line (outgoing tree first); absolute paths of files outside the workspace taken | 99 calls, 8 misuses; `trace` 28 calls. One answer wrong: `callers` trusted an incomplete call hierarchy (C4LSP missed calls through `->`) and named 2 of 4 - so `code_references` and incoming `code_call_hierarchy` now list the lines naming the symbol that the server did not report |
| 2026-10-22 | same | C4LSP fixed (references from a declaration, calls through `->`; a1b172d); the unlisted-lines note | `callers` right in 2 calls (4 of 4), `change` complete in 7; text search nearly gone. `trace` fell back to reading whole files (16) and hit two bugs: `fs_read` refused the absolute path the code tools show for a library file, `code_symbols` on a folder said "no language server serves" - both fixed |
| 2026-10-22 | same | `fs_read` takes those absolute paths; a folder for `code_symbols` is said to be one | rerun of `trace`, `members`, `errors`: `trace` used `code_call_hierarchy` (3 times), 40 of 47 calls code tools, 5 misuses (was 15); `members` and `errors` 2 each |
