# 13 - AI tool catalog

## Purpose

The complete set of tools IED offers models, with enough shape that the implementer can write schemas directly. The plan asks for "any tool calls that you can think of for writing code and working with games/game engines"; this is that list, organized so a **local 27B model is not drowned** in 100 tool definitions.

## Design rules for tools

1. **Small always-on core, groups on demand.** Every request carries the *core* toolset (about 15 tools). Other groups are listed by name + one line in the system message and enabled with `tools_enable(group)` (or automatically when a loaded skill declares it needs them, or when the mode's preset includes them). Enabled groups stay enabled for the rest of the turn. This keeps prompts short and prefix-cache friendly.
2. **snake_case names, `group_verb` shape** (`fs_read`, `lsp_references`). Descriptions are one or two sentences plus one example; parameters have tight types and enums.
3. **Bounded results with continuation.** Every list/read result has a size budget; when truncated it says exactly how to get the rest (`offset`, `page`, narrower query).
4. **Errors teach.** An error result names what was wrong and what to try ("path not found; did you mean src/Core/Ring.cf?").
5. **Symbol-first navigation.** Tools take symbol names as well as positions, so the model can say `lsp_definition(symbol: "Ring::Push")` instead of computing line/column.
6. **Same plumbing as the human.** Build tools run toolbar tasks; shell runs in visible terminals; edits go through the document store. Nothing the AI does is invisible.
7. **Every tool declares:** `class` (read / write / exec / net / meta), `parallelSafe`, `group`, `owner`, result budget, and whether it is available in untrusted workspaces.

Permission handling per class is defined in `docs/11`.

## Core toolset (always on)

| ID | Tool | Class | Purpose |
|---|---|---|---|
| T-FS-1 | `fs_read(path, offset?, limit?, around_line?)` | read | Read a text file (line-numbered). Large files return the first window + outline. |
| T-FS-2 | `fs_write(path, content)` | write | Create or overwrite a file (through the document store; checkpointed). **Built** with `fs_edit`: where the file's language plugin asks for it, the file is formatted by its language server and the result carries the server's diagnostics of it and the other files it finds problems in - never a failure of the call (`docs/24` "After an AI edit"). A plugin's folder (`plugins:/`, "Plugin folders" above) refuses both. |
| T-FS-3 | `fs_edit(path, edits: [{old, new, replace_all?}])` | write | Exact-match replacements, all-or-nothing; an `old` not in the file is answered with where it differs (**built**: the line where the file agrees longest with the start or the end of `old`, and what the file has there against what `old` has, line breaks and tabs shown as escapes). |
| T-FS-4 | `fs_list(path, depth?, glob?)` | read | Directory listing (respects ignore rules). |
| T-FS-5 | `fs_glob(pattern, root?)` | read | Find files by glob (`*`, `?`, `**`, `{a,b}`; `src/fs/glob`). |
| T-SRCH-1 | `search_text(pattern, regex?, glob?, path?, scope?, context_lines?)` | read | ripgrep-backed workspace search; scopes: project / solution / member / federation. **Built**: `path` a folder to search under (`glob` then relative to it), `ied:/...` or `plugins:/<id>/...` for those roots. |
| T-SHELL-1 | `shell_run(command, cwd?, timeout_s?, background?)` | exec | Run in the chat's AI terminal; returns exit code + output tail; `background` starts it as a background task and answers at once with its id (built 2026-10-23, `background` below). |
| T-LSP-1 | `code_symbols(path or query, kind?, depth?)` | read | Outline of a file (kinds, signatures, line ranges), or the workspace's symbols named like `query`. **Built**, "Navigating code". |
| T-LSP-2 | `code_definition(target)` | read | Where it is defined and declared, each with its first lines. **Built**. |
| T-LSP-3 | `code_references(target, limit?, include_declaration?)` | read | References by file, each line with the function it sits in. **Built**. |
| T-LSP-4 | `code_read_symbol(target, declaration?)` | read | The full source of one function/type/member/macro with the comment above it, the definition before the declaration - the main way to read code without whole files. **Built**. |
| T-BLD-1 | `build_run(task?: "build", project?, config?, timeout_s?)` | exec | Run a toolbar task of `project` (the startup project when left out; its schema says so since 2026-10-23, after a test run on a startup project without tests failed unexplained); returns status + structured problems (from the Error List), not raw logs. **Built**: each error with the source line it names under it, and the language plugins' hints (`aiHints`, `docs/26`) under the errors they match; the task is cancelled (its process trees killed) when the run is stopped or `timeout_s` passes, and the result says which (a stopped turn used to wait for a test that ran for minutes). |
| T-BLD-2 | `problems_list(scope?, severity?, source?)` | read | Current Error List (build + LSP + spell) as structured items; source lines and hints as `build_run`. |
| T-BLD-3 | `program_run(program, args?, stdin?, cwd?, timeout_s?, repeat?)` | exec | **Built**: a program run without a shell (arguments as given, nothing to quote): its exit code, time (each of `repeat` runs and their median), CPU time and peak memory (read once it ended), and its stdout and stderr apart, their ends. The program by its workspace path, as its build prints it, or by its name on PATH; `stdin` a workspace file. For trying a command-line program and timing it. |
| T-META-1 | `tasks_set(items)` / `task_update(id, status, note?)` | meta | Chat task list (F-CHT-80). |
| T-META-2 | `ask_user(question, options?, allow_text?)` | meta | Structured question; the run waits for the answer. |
| T-META-3 | `tools_enable(group)` | meta | Enable a tool group for the rest of the turn. |
| T-META-4 | `finish(summary, verification)` | meta | Agent mode only: end the turn with what was done and how it was verified. |

## Loading tools on demand (built, 2026-10-21)

Every tool definition sent costs context at every step, and the list grows with each plugin (the browser's and the profiler's tools alone are about 2,500 tokens). So the core set is always sent, and the rest is *deferred*: a plugin's tool calls (unless the entry says `"preload": true`) and the groups below (as built: `media`). A deferred tool is in every request's `tools` from the chat's first step as a *stub*: its name, its description's first sentence ending in `[deferred]`, and parameters `{"type": "object"}`; the system message says once what the mark means ("Deferred tools", `docs/14` item 7). `tool_search(query)` - words for what the model needs, or `select:name,name` - answers with up to 5 of them whole, description and schema, at the end of the conversation; a deferred tool can also be called straight away, its arguments checked against its real schema, which a mistake gets back in the error. **Loading changes nothing at the front of the prompt** (2026-10-26, owner: "loading a tool call loads it in the system message causeing the full context to get re-processed. it would be better to just append it to the end"): until then a loaded tool joined the request's `tools` and left the system message's list, both of which a model server's chat template puts before the conversation, so the next step's prefix differed from the cached one at its first bytes and the server read the whole chat again - minutes on a long local chat, and the cold read the prefill watchdog then mistook for a stall (`docs/12` "What makes a server read the whole prompt again"). A Claude chat gets every schema whole (the CLI's own tool list), and `ai.tools.loadAll` (app, solution; off) sends every definition whole, for a model that does better without the extra step. What a chat looked up stays recorded (`chat.json` `loadedTools`). As built: `plugins/chat/code/tool.cf` (`RequestTools`, `SearchDeferred`), `runtools.cf` (`tool_search`); server checks `test_chat_prefix.py`.

## Plugin folders (built, 2026-10-26)

Owner (item 23 of `docs/30`): plugins "can have there own skills that are avaliable to the model in the chat but are
not in the skills folder for that project but rather they are in the plugins folders. they are write locked unless
developer mode is enabled", and the same for the other docs a plugin carries (the c4 language docs). As built
(`plugins/chat/code/roots.cf`, the API's `IED::LoadedPlugins` and `IED::SearchFolder`):

- **`plugins:/<plugin id>/<path>`** names `<path>` in the folder of a plugin loaded for the chat's workspace - a root
  alias like developer mode's `ied:/` (`docs/28`), one alias for every plugin, the plugin's id its first part
  (`plugins:/c4/docs/library/bedrock-api.md`, `plugins:/ied.nuke/skills/nuke/SKILL.md`). `fs_read`, `fs_list`,
  `fs_glob`, `search_text` (its new `path`: the folder it searches under) and `image_info` read there, from the disk
  (a plugin's folder is no workspace: the tools that need one, `code_*` and the others, say they do not work there);
  `fs_list({"path": "plugins:/"})` lists the plugins (`plugins:/<id>/  <name> (builtin|app|project)`). Paths in their
  results carry the alias, so the model passes them back as they are; the same now holds in `ied:/`. The permission
  subject names the plugin: `fs.read(plugins:/c4/docs/...)`.
- **Read-only.** A write, an edit or a command there (`fs_write`, `fs_edit`, `shell_run` with `cwd: "plugins:/..."`,
  any tool not of class read) is refused - "plugins:/c4/ is read-only: a plugin's files (its skills, docs and manifest)
  change only through a root that holds its folder (...), which developer mode gives when IED runs from its source" - and
  so is a write that reaches a loaded plugin's folder by its workspace path (a toolchain's plugin the solution pins in
  `localPlugins`, the c4 plugin at `toolchains/c4/dev/ied`). Except: a root holds the folder, and then the write goes
  there - developer mode's `ied:/` holds the built-in plugins of a server run from its source, and of one installed
  from it (`ai.roots` gives `home: true`: `<home>/plugins/chat` is `ied:/plugins/chat`); the result says where it went
  ("[plugins:/ied.chat/ is ied:/plugins/chat/: the change was made there]") and the write follows the developer-mode
  rules (`docs/28`). A plugin the workspace holds in `.ied/plugins/` is the workspace's own: writes to it go through
  (by `plugins:/` too, into `.ied/plugins/<name>/`). A workspace that is itself a plugin's folder (the owner working on
  an app plugin) is its own too.
- **Where a skill or a doc lives.** `skill_read` ends with "[This skill is plugins:/c4/skills/c4-language/SKILL.md,
  read-only: a plugin's file. Beside it in plugins:/c4/skills/c4-language/: 01-what-is-c4.md, language/, ... - read
  them with skill_read and file, or fs_read and fs_list there]" (a project skill names `.ied/skills/...`; the app's
  skills folder is out of the file tools' reach and names none); `docs_read` names the file after its path
  ("c4/library/bedrock.md (plugins:/c4/docs/library/bedrock.md, read-only)"). The system message's "Skills" and
  "Reference docs" sections say a plugin's skills and docs are in its folder, `plugins:/<plugin id>/`, read-only
  (`docs/14`).

## Groups (enabled on demand)

### `fs_more`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-FS-10 | `fs_move(from, to)` | write | Also updates open buffers; offers LSP rename-file refactor when available. |
| T-FS-11 | `fs_delete(path)` | write | To recoverable trash. |
| T-FS-12 | `fs_stat(path)` | read | Size, mtime, encoding, EOL, indentation style, git status. |
| T-FS-13 | `fs_diff(path, against?: "saved" | "head" | "<rev>")` | read | Unified diff of a file. |
| T-FS-14 | `fix_indentation(path, range?, style?: "tabs" | "spaces:<n>")` | write | The indentation fixer (F-EDT-31), default = project style (tabs). |
| T-FS-15 | `fs_apply_patch(patch)` | write | Unified-diff patch application (for models that prefer patches). |

### `code` (LSP extras; built 2026-10-21 but for T-LSP-13..16; deferred but for T-LSP-12 and T-LSP-18)

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-LSP-10 | `code_hover(target)` | read | What the server says: declaration, type, documentation. **Built**. |
| T-LSP-11 | `code_implementations(target)` | read | **Built**. |
| T-LSP-12 | `code_call_hierarchy(target, direction: incoming|outgoing, depth?)` | read | Callers or callees as a tree with the lines of each call; when the server has a call hierarchy. **Built**; core since 2026-10-22 (a local model never loaded it from the deferred list, `tools/ai-evals`). |
| T-LSP-13 | `code_diagnostics(path?)` | read | LSP diagnostics for a file or the workspace. (`problems_list` covers it today.) |
| T-LSP-14 | `code_rename(symbol, new_name, preview?)` | write | WorkspaceEdit applied through the document store. |
| T-LSP-15 | `code_format(path, range?)` | write | Language formatter. |
| T-LSP-16 | `code_actions(path, line, apply?: id)` | write | List/apply quick fixes for a location. |
| T-LSP-17 | `code_custom(method, path, line?, column?, symbol?, params?)` | read | A request the serving profile lists in `aiExposedRequests`; with `line`, the params get the position. **Built**. |
| T-LSP-18 | `code_members(target, inherited?, limit?)` | read | A type's members, each with its declaration as the server shows it (types, return type, parameters, `static`, `const`, `inline`, `virtual`), its semantic-token modifiers and doc summary; `inherited` adds the base types' (type hierarchy). **Built**; core since 2026-10-22, as T-LSP-12. |
| T-LSP-19 | `code_type_definition(target)` | read | The type of a variable or expression and where it is defined. **Built**. |
| T-LSP-20 | `code_type_hierarchy(target, direction: supertypes|subtypes, depth?)` | read | Base or derived types as a tree. **Built**. |
| T-LSP-21 | `code_expand_macro(target)` | read | What a macro use expands to: the request a profile's `aiExposedRequests` marks `purpose: "expandMacro"`, else the hover (clangd shows the expansion) and the definition. **Built**. |

### Navigating code (built, 2026-10-21)

The `code_*` tools let a model read code by symbol instead of whole files, through the workspace's language servers - the same ones the editor uses (`docs/24` "Plugin code asks language servers"; nothing language-specific in IED). As built: `plugins/chat/code/lspquery.cf` (requests, answers read, a name found), `codenav.cf` (the four core tools), `codeinfo.cf` (the `code` group), `src/api/lsp.ch` (the host API, permission `lsp`).

- **A target** is `symbol` - a name, qualified to narrow it (`Ring::Push`, `game.Ring.Push`; `::` and `.` alike) - or `path` and `line` (and `column`, or `symbol` naming the word on that line, else the line's first word). A name is looked up with `workspace/symbol` (in `path`'s outline when given), matched on its last part and its owners, and given its whole range from its file's outline. A name of several symbols with one qualified name (declaration and definition, overloads) is one target; of different ones, the call fails listing them with their places; a type and its own constructor are the type. A name nothing has fails with similar names.
- **Answers** are compact text: lines from 1, workspace paths (a file outside the workspace that a server named - a library header - as its absolute path, and readable through these tools), symbols as `kind qualified-name: detail`, source numbered as `fs_read` numbers it. A search by name whose only symbol is at most 40 lines comes with its source (a search, then a read of what it found, was a local model's most common wasted step); a search by a name's words keeps the 25 symbols holding the most of them; `code_members` shows each declaration without its owner's qualification. `search_text` names each file once with its matching lines under it, each cut at 160 bytes.
- **What the first search does.** `workspace/symbol` reaches only running servers, so the first one starts the servers the workspace's files call for and opens one file on each (clangd only finds `compile_commands.json` from an open file), then waits while they index (`$/progress`, at most 30 s).
- **What a server cannot do** is said with the tool that can: no call hierarchy - `code_references`; no type hierarchy - `code_implementations`; no expand request - the hover and definition.
- **What a server may miss.** `code_references` and incoming `code_call_hierarchy` also search the workspace's files for the name as a whole word and list the lines that use it as code (a call, `.name`, `->name`, `name::`, `name<`, `Name* x`; not in a comment or a string) which the server did not report - uses it can miss (through a pointer, a macro, dynamic dispatch) or other symbols of that name - so an answer is never taken for complete when it is not (a model trusted an incomplete call hierarchy and named 2 callers of 4 before this); a name other symbols also have that more than 25 other lines use is counted instead of listed, as a common one, and lines naming a name no other symbol has are said to be uses the server missed.
- **Checked** against clangd (`tools/server-checks/tests/test_code_tools.py`: outline, search, a method's body with its comment and declaration, references by function, members with `static`, `inline bool Empty() const [readonly]`, `override [virtual]`, inherited `= 0 [abstract]` and doc summaries, type definition, implementations, both call directions, subtypes, macro expansion) and the fake server (plugin requests, transient opens); unit cases `server.ai.codetools`. Against C4LSP (`docs/17` L10-L14) on a project as `nuke init --ied` and `nuke build` make it: outline with member kinds and signatures and macros, the definition and its header declaration with their comments, references from a use, hover, members with `static`, `const`, annotations, a constant's value, doc summaries and the interface's inherited ones, both call directions, subtypes, `c4/expandMacro`, references asked from a declaration, a variable's type definition, implementations as methods and a macro read by name (`docs/17` L15-L18).
- **Tuned with a real model** (`tools/ai-evals/codenav_eval.py`, 2026-10-22): eight questions about the server's own sources, each in a read-only chat, with the owner's local Qwen 27B. It chose the code tools on its own and answered correctly, but never loaded a deferred tool, searched text for identifiers a code tool had just found, sent regexes as literal text, and called `code_read_symbol` with only a path. So `code_members` and `code_call_hierarchy` became core, the system prompt got a "Navigating code" section (which tool for which question; `docs/14`), `code_symbols` searches a name nothing has by its words (`LanguageServerPool` finds `LspPool`), `search_text` reads a literal pattern that matches nothing as the regex it looks like, empty results say what to try, and a file outside the workspace is taken by the absolute path the tools show. Misuses halved and the deferred tools were used where the question called for them; see `tools/ai-evals/README.md` for the runs.
- **Tuned for time and tokens** (2026-10-22, the same eval at temperature 0, one turn at a time, quality held by fact checks on each answer): a turn's time is mostly the model generating - each step's thinking and call arguments, then the answer - so steps and output are what to cut. A search by name gives its lone match's source (a search, then a read of what it found, was the commonest wasted step, and no symbol was read again after it), the word search keeps its best matches, members drop their owner's qualification, `search_text` is grouped by file, and the unlisted-lines note lists only uses as code; the system prompt asks for independent calls in one reply, a plain name read directly, a stop once the question can be answered, and answers without filler. 853 -> 730 s, 77 -> 56 steps, 153 -> 109 calls, 38/38 facts throughout. Since: C4LSP reports a constructor's constructions and a class's uses in other files (a472b79), which `trace` had spent its steps looking for, and a role's effort reaches the model (`docs/04`), which cuts the thinking - about half the output: 730 s at the model's default, 494 s at medium.

### `build`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-BLD-10 | `task_list()` | read | Available tasks/toolbar buttons and dropdown values per project. |
| T-BLD-11 | `task_run(task, vars?)` | exec | Any task (run, clean, profile, custom). |
| T-BLD-12 | `output_read(source, tail?, grep?)` | read | Read an output source (Build, Run, Test...). |
| T-BLD-13 | `tests_list(project?)` / `tests_run(filter?, project?)` | exec | Structured test results (names, pass/fail, messages, locations). |
| T-BLD-14 | `process_stop(handle or task)` | exec | Cancel a run or background process. |

### `debug`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-DBG-1 | `debug_start(config?, stop_on_entry?)` | exec | Uses the project's debug configuration. |
| T-DBG-2 | `debug_breakpoint(path, line, condition?, remove?)` | exec | |
| T-DBG-3 | `debug_control(action: continue|step_over|step_in|step_out|pause|stop, thread?)` | exec | Returns the new stop location + reason. |
| T-DBG-4 | `debug_state(thread?, frames?: n)` | read | Threads, stack, current location with source excerpt. |
| T-DBG-5 | `debug_variables(frame?, scope?, expand?: path, depth?)` | read | Locals/scopes, bounded expansion. |
| T-DBG-6 | `debug_evaluate(expression, frame?)` | read | REPL-context evaluate. |
| T-DBG-7 | `debug_wait(timeout_s)` | read | Wait for the next stop/exit event. |

### `git` (built, 2026-10-22; deferred)

The `ied.git` plugin's tools: C4 functions of the plugin (`plugins/git/code/tools.cf`, `toolread.cf`, `toolchange.cf`; their text `tooltext.cf`, `tooldiff.cf`; paths `toolplace.cf`) answering through its `git.query` binding from the workspace's repositories, with git run as the Git activity runs it (no prompts, no optional locks, machine formats; `docs/10` "Git"). Paths are the workspace's, as `fs_read` takes them: a repository in a folder of the workspace (`lib`) has its paths under `lib/`, and one above the workspace's folder is seen only inside the folder (its diffs `--relative`, its other changes counted, not listed). A call works in the repository `repo` names (its folder, `.` the workspace's own), else the one holding its paths, else the workspace's own or its only one; paths of two repositories in one call are refused. A system-message section (`Git::Section`, when the workspace has a repository) names the repositories, says to read them with these tools rather than git in a shell, and to stage, commit, branch or stash only when the owner asks. The reading tools are offered in Plan mode; the others ask in Ask mode (subject `plugin(ied.git/<tool>)`, `docs/11`), log their commands to the Git output, read the repository again (the Git activity follows at once) and answer with its status after them. None pushes, pulls or fetches, and none throws work away (no `branch -D`, stash drop, reset or discard).

| ID | Tool | Class | As built |
|---|---|---|---|
| T-GIT-1 | `git_status(repo?)` | read | Every repository's (or one's) branch, its upstream with ahead and behind (or gone), a merge, rebase, cherry-pick, revert or bisect in progress, then the conflicted, staged, not staged and untracked files in words (`renamed: b.c (from a.c)`), 200 of each. |
| T-GIT-2 | `git_diff(paths?, staged?, rev_range?, context?, repo?)` | read | What is not staged (git's own default), what is staged, or against or between revisions (`rev_range`: `HEAD`, `main..feature`); each file's +/- counts first, then the diff cut at a line past 24 KB with what was left out said; after an unstaged diff, the untracked files. With the default context each hunk header ends with `(hunk N)`, the numbers `git_stage` and `git_unstage` take. |
| T-GIT-3 | `git_log(n?, skip?, rev?, all?, paths?, query?, author?, repo?)` | read | A line a commit: short hash, date, author, subject and the refs on it; 30 (at most 200), then `[more: skip N ...]`. `query` searches messages in any case, `author` names or emails. |
| T-GIT-3 | `git_show(rev?, paths?, file?, offset?, repo?)` | read | A commit's hash, parents, author, committer and whole message, then its change against its first parent (a merge's too) with counts; with `file`, that file at the revision, numbered, 400 lines a call. |
| T-GIT-3 | `git_blame(path, start_line?, end_line?)` | read | A work tree file's lines with their commits: each commit once (short hash, date in its author's zone, author, subject), then `N hash<TAB>text`; lines not committed yet marked; 400 lines a call. |
| T-GIT-6 | `git_branches(repo?)` | read | The local branches (the current one marked, upstream, ahead, behind or gone, last commit), the remote branches, the tags, the stashes. |
| T-GIT-4 | `git_stage(paths, hunks?, repo?)` / `git_unstage(paths, hunks?, repo?)` | write | Whole files and folders (`add -A`; `restore --staged`, before the first commit `rm --cached`), or hunks of one file as `git_diff` numbers them (each hunk's patch built as the activity's `git.stagePatch` builds it, applied last first). |
| T-GIT-4 | `git_commit(message, paths?, amend?, repo?)` | write | What is staged; with `paths`, exactly those files as the work tree has them (new ones staged first, then `commit --only`), other staged changes left staged. `amend` is refused once HEAD is on its upstream. Answers with the new commit's hash, subject and stat. |
| T-GIT-5 | `git_branch(action: create|switch|delete, name, from?, switch?, repo?)` / `git_stash(action: push|pop|apply, message?, untracked?, index?, repo?)` | write | A remote branch switched to (`origin/x`) gets a local branch tracking it; delete takes only merged branches (`-d`). A switch or stash that would overwrite local changes fails with git's words and what to do. |

**What the approval card shows** (built 2026-10-22, each write tool's `plan`, `docs/26` section 8; `plugins/git/code/toolplan.cf`, through `git.query` with `plan: true`): `git_stage` and `git_unstage` with `hunks` show those hunks under their file, numbered as `git_diff` numbers them, from the same `git diff` the tool then applies; with whole paths, those files' diff (`git diff`, or `--cached` to unstage), the untracked files `add -A` would take as new files. `git_commit` shows its subject, its branch and its files with their counts over their diff: what is staged (`diff --cached`), or with `paths` exactly those files as the work tree has them against HEAD, new ones too; an amend names the commit it replaces. A diff past 24 KB keeps its first files' lines and names the rest with no lines. `git_branch` and `git_stash` say what they will do in a line ("Create branch fix-wrap at HEAD and switch to it", "Stash the local changes: 3 files, +12 -4, untracked files too", "Pop stash@{0} (On main: wip)"). A call that cannot work - a hunk that is not there, nothing staged, an amend of a pushed commit, a stash or branch that does not exist - fails at its plan, without asking.

Checked: unit cases `server.git.tools` (paths both ways, status texts, hunk numbers, a cut diff, log paging, blame dates in the author's zone, branches, a file's text); server checks `test_git_tools.py` against real repositories (hunks staged and unstaged by number, a commit of named paths that leaves the rest, an amend refused once pushed and allowed before, log, show, blame, branches and stashes, a repository in a folder and one above the folder, untrusted), and a chat whose fake model reads the status and commits once the owner approves, in Plan mode offered only the reading tools.

### `web`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-WEB-1 | `web_search(query, engines?, count?, page?, time_range?, language?)` | net | The `ied.websearch` plugin's SearXNG, its engines asked through Tor (`ied.tor`). |
| T-WEB-2 | `web_fetch(url, format?: markdown|text|raw, start?, max_chars?)` | net | The `ied.webfetch` plugin: fetched looking like a browser, HTML -> Markdown, long pages a window at a time. Not yet: PDF -> text, a `summary` format (the `summarizer` role). |

**As built (2026-10-21, `docs/29`):** both are C4 functions of their plugins over their bindings (`websearch.search`, `webfetch.fetch`), in the `web` group, deferred; each plugin adds a one-line system-message section naming its tool. `web_search` lists numbered results (title, URL, snippet, the engines that found it, a date) with SearXNG's answers, suggestions and the engines that failed; a search that engines refused through the Tor exit is tried once more on new circuits. `web_fetch` returns a header (URL, status, type, title, length) and a window of the text, names bot checks ("Just a moment...") and suggests `browser_open` for them, PDFs and pages drawn by scripts.

### `browser` (headless Edge via CDP, server-side; built in `plugins/browser`, `docs/27`)

As built: `browser_open(url, width?, height?)`, `browser_navigate(page, url)` (T-BRW-8), `browser_snapshot(page, mode)`, `browser_screenshot(page, full_page?)` (the PNG in `.ied/local/browser/shots/` and as an image), `browser_act(page, action, ref?, selector?, text?)`, `browser_console(page, since?)`, `browser_network(page, since?)`, `browser_eval(page, js)`, `browser_close(page)`; each a C4 function over a `browser.*` server binding. No `element` screenshot yet.

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-BRW-1 | `browser_open(url, viewport?)` | net | Returns a page handle; localhost URLs allowed (dev servers). |
| T-BRW-2 | `browser_snapshot(page, mode: text|a11y|dom)` | read | Accessibility-tree snapshot with element refs. |
| T-BRW-3 | `browser_screenshot(page, element?, full_page?)` | read | Image (to vision models) + saved file. |
| T-BRW-4 | `browser_act(page, action: click|type|press|select|scroll|hover, ref or selector, text?)` | net | |
| T-BRW-5 | `browser_console(page, since?)` / `browser_network(page, filter?)` | read | Console messages and failed requests. |
| T-BRW-6 | `browser_eval(page, js)` | exec | Gated (`ask` in default mode). |
| T-BRW-7 | `browser_close(page)` | meta | |
| T-BRW-8 | `browser_navigate(page, url)` | net | Another URL in an open page; waits for its load (not for a `#fragment` change, which loads nothing). |

### `media`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-MED-1 | `image_view(path or url)` | read | Sends the image to a vision model; non-vision groups get a text description from the `vision` role. **Built** for a workspace path (`plugins/chat/code/media.cf`): offered with the core tools until groups and `tools_enable` exist; a model that does not take images is told the path. Not yet: a URL, the `vision` role's description. |
| T-MED-2 | `video_frames(path, every_s? or count?, range?)` | read | Extracts frames (bundled ffmpeg or Media Foundation) and sends them as images. |
| T-MED-3 | `image_compare(a, b, threshold?)` | read | Pixel diff: changed-pixel %, bounding boxes, diff image - for visual regression in games/UI. |
| T-MED-4 | `image_info(path)` | read | Dimensions, format, mips, alpha, color space where detectable. **Built** (`plugins/chat/code/imageinfo.cf`): PNG, JPEG, GIF, WebP, BMP and DDS from the first 256 KB - "DDS, 1024x2048, 12 mips, cube, BC7_UNORM_SRGB (sRGB)"; with the core tools until groups exist. |
| T-MED-5 | `screenshot_window(process or title)` | read | Capture a running app's window (Windows Graphics Capture) - e.g. the game being developed. |
| T-MED-6 | `screenshot_ied(view?)` | read | Screenshot of an IED view (for plugin/UI development and self-verification). |

### `system` (process and machine)

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-SYS-1 | `process_list(filter?)` / `process_stats(pid)` | read | CPU, memory, handles, threads for a process - quick perf/leak checks. |
| T-SYS-2 | `shell_read(handle, tail?)` / `shell_kill(handle)` | exec | For background commands. |
| T-SYS-3 | `env_info()` | read | OS, CPU, GPU, installed toolchains the plugins report. |

### `knowledge`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-KNW-1 | `skill_search(query)` | read | **Built**: the workspace's skills (`.ied/skills/`, plugins' `skills`, the app's; `docs/26` section 8) whose name, description or keywords hold the query's words, a word in the name counting most; up to 10 as `name (source): description`. BM25 and embeddings later. |
| T-KNW-2 | `skill_read(name, file?)` | read | **Built**: a skill's SKILL.md without its frontmatter, or another file of its folder (`file`, relative to it); an unknown name lists the skills; it says where the skill lives and what lies beside it ("Plugin folders" above). Enabling the tool groups a skill declares comes with tool groups. |
| T-KNW-3 | `skill_write(name, description, keywords, body)` | write | Approval-gated; writes to `.ied/skills/` or app tier. |
| T-KNW-4 | `history_search(query)` / `history_read(turn or range)` | meta | **Built** (2026-10-25, `plugins/chat/code/history.cf`, deferred tools of group `chats`): what this chat said in the turns compression left out of its requests. The tool the "earlier parts were left out" note points at - without it those turns are out of reach (F-AIR-54). Search gives the newest place each word was said, read gives a turn's messages as the model was sent them (at most 4 at a time). |
| T-KNW-5 | `scratchpad(action: read|write|append|replace_section, section?, text?)` | meta | Chat scratchpad (F-CHT-71). |
| T-KNW-6 | `memory_read()` / `memory_add(fact)` | meta | Project memory (P2, F-AIR-66). |
| T-KNW-7 | `docs_search(query)` | read | **Built**: the reference docs of the workspace's language plugins (`aiDocs`, `docs/26`: a language's manual, its library's API) searched by words - the best Markdown sections with their path, line range and the lines that match. |
| T-KNW-8 | `docs_read(path, heading? or line?)` | read | **Built**: a section of those docs by its path (`c4/library/bedrock.md`) and heading or line, as `docs_search` names them; a path alone lists the file's headings; it names the file in its plugin's folder (`plugins:/<id>/...`, read-only). |

### `agents` (built, 2026-10-23; offered while the chat may run agents)

A group's sub-agents (`docs/04` "A group is a topology"); the first design's `spawn_agents`, `agent_send`, `agents_wait` and `agents_cancel` became these with `chat_send` and the `background` tools.

| Tool | Class | Notes |
|---|---|---|
| `agent_list()` | read | the agents this chat may run: model, local or cloud, mode, what each is for, each one's record in the workspace (runs; finished, failed, stopped; median time and tokens) and which run now, then when delegating pays |
| `agent_run(agent, task, chat?)` | exec | starts the agent as a chat of its own on `task`, as a background task of this chat (its id answers); `chat` continues a sub-agent chat that ran before; its answer comes back when its turn ends. A brief under 200 characters gets a reminder; at most 4 running, 3 chats in a row; subject `agent.run(<name>)` |
| T-AGT-3 `submit_plan(plan_markdown, tasks)` | meta | Plan mode output (not built). |

### `background` (built, 2026-10-23; deferred)

What `shell_run` and `build_run` with `background`, and `agent_run`, started (`docs/04` "Background tasks"). A task's end is told to its chat by itself: a harness note at the running turn's next step, or a turn of its own for an idle chat.

| Tool | Class | Notes |
|---|---|---|
| `background_status(id?)` | read | the chat's tasks (state, time, how each ended), or one with the end of its output (an agent's answer) |
| `background_wait(id?, seconds?)` | read | until that task, or any of the chat's, ends (120 s by default, at most 600); an ended task it shows is told |
| `background_stop(id)` | meta | a command killed, a project's task cancelled, an agent's turn stopped |

### `chats` (built, 2026-10-21; deferred)

Chats of one workspace talking to each other (`docs/04` "Chats talk to each other").

| Tool | Class | Notes |
|---|---|---|
| `chat_list()` | read | id, title, mode, state, the first line of each last answer; this chat marked |
| `chat_read(chat, turns?)` | read | the last turns (at most 5): messages and answers, no tool calls |
| `chat_send(chat, text)` | exec | starts a turn there or reaches its running turn; subject `chat.send(<id>)`; at most 3 hops |
| `chat_create(text, title?, mode?, model?)` | exec | a new chat started with `text`, on this chat's group and pinned model or `model` (a cheaper one for routine work); subject `chat.create(*)` |
| `chat_wait(chat, seconds?)` | read | until its turn ends (default 120, at most 600 s); returns the answer |

### `models` (built, 2026-10-22; deferred)

The models a chat runs on, and Claude's plan usage, so a lead chat can keep the strongest model for itself while it is
under pace and give routine work a cheaper one (`docs/04` "Pacing", "Switching models"). As built:
`plugins/chat/code/modeltools.cf`.

| Tool | Class | Notes |
|---|---|---|
| `ai_usage(refresh?)` | read | Claude Code's version and whether it is on the Claude subscription, the plan; each window (the 5-hour session, the week, the per-model weeks): percent used, when it resets, how far through it is, its pace; when Claude frees if it is used up; each `ai.models` profile; each group role whose chain has a Claude model, with every place's pace and state (over its pace, used up) and the model a turn of it uses now (a chat's mode picks the role); this chat's pinned model. `refresh` asks the CLI first. Subject `ai.usage(*)` |
| `chat_set_model(model, chat?)` | exec | pins a model of `ai.models` for a chat's next turns (this chat's, or another's by id), `""` back to the group's; the current turn keeps its model. An unknown name fails listing the names. Subject `chat.model(<name>)` |

### `clipboard` (built, 2026-10-21; deferred)

A chat's own clipboard, so the model moves, repeats or matches text it has seen without typing it again: a long `old` for `fs_edit` taken exactly from the file (no lost tab, no wrong line), a function moved to another file, an error message reused in a search. As built: `plugins/chat/code/clipboard.cf`.

| Tool | Class | Notes |
|---|---|---|
| `clipboard_copy(name, path? / result? / text?, start_line?, end_line?, from?, to?, append?)` | read | One source: `path` a workspace file as the editors have it (subject `fs.read(<path>)`), `result` an earlier tool result of the chat (1 the latest, the clipboard's own not counted; a line-numbered result such as `fs_read`'s loses its gutter and `start_line`/`end_line` are the numbers it shows), or `text`. `start_line`/`end_line` take whole lines with their breaks (a CRLF break read as LF); `from`/`to` then narrow it from the first `from` to the end of the first `to` after it. `append` adds to the clip. A clip holds at most 64 KB, the clipboard 20 (the oldest dropped, said in the result). The result gives the size and the first and last line, not the text; the card shows the text. |
| `clipboard_list()` | read | Each clip's name, size, source and first line. |

**Pasting.** `{{clip:<name>}}` in any string of any later call's arguments - any tool, a plugin's too, at any depth (`edits[].old`) - is replaced by the clip before the arguments are checked against the tool's schema, so the permission check, the approval card's diff and the tool see the pasted text, while the model's own call, which later requests send, keeps the token (no context spent twice). `{{!clip:<name>}}` writes `{{clip:<name>}}` itself; text that is not a well-formed token (`{{clip:<name>}}` as written here, angle brackets and all) stays as it is, and pasted text is not pasted into again. A token naming a clip the chat does not have fails the call without running it (harness note `unknown_clip`, naming the clips there are). Clips are names of letters, digits, `_`, `.` and `-`, at most 40. They are kept in `chat.json` (a restart keeps them), are the chat's own (another chat starts empty), and show in the details panel's "Clipboard" section (`clips.changed`, `docs/10`), each unfolding to its first 2 KB. `fs_edit`'s description points at the clipboard for long blocks.

### `projects` (built, 2026-10-22; deferred)

Project management for the AI (owner, 2026-10-22: making, moving and removing projects and making an existing folder an IED project, each change showing in the running Solution Explorer without a reload). Every tool runs the server's `project.*` operation (`docs/03` "Managing projects", `docs/10` "Projects") through `api/projects.ch`, so a change reaches every window at once - a folder workspace made a solution becomes one in place - and answers with what it did, IED's notes (the language and build preset it found, a file to check), the workspace's projects as they are now and the end of the build system's output. As built: `plugins/chat/code/projecttools.cf`.

| Tool | Class | Notes |
|---|---|---|
| `project_list()` | read | The workspace (kind, folder, trust) and its projects: kind, language, folder, project file, the projects each depends on; the other open workspaces. A folder workspace is told how to become a solution. |
| `project_templates()` | read | New Project's templates by key (`ied.cpp/console`), with their language and description, which can be added to this solution, and where a new workspace goes. |
| `project_create(template, name, location?)` | exec | Without `location`, a project added to this solution (the template must be able to add to it: its error says to give a location instead); with one, a new workspace `<location>/<name>`, opened and trusted beside this chat's (the result says the chat stays in its own). Subject `project.create(<name>)`; adding to an untrusted solution fails at the plan. |
| `project_import(folder, name?, kind?, language?)` | exec | `.` makes this workspace a solution in place; a folder of the workspace joins its solution as a project; a full path elsewhere becomes a solution of its own and opens. A `.iedsln` already in the folder (a generator such as `nuke init --ied` wrote it) is opened as it is; else the build system's own project files when a project system owns the folder (`nuke init --ied`, `nuke project add`), else IED's `.iedsln` and `.iedproj`. Subject `project.import(<folder>)`. |
| `project_move(project, to)` | exec | The project's folder to another folder of the workspace; documents open in it follow, the build system's manifests and the solution too. Subject `project.move(<project> -> <to>)`. |
| `project_remove(project, delete_files?)` | exec | Out of the solution and its build, its files kept; `delete_files` moves its folder to IED's trash. Refused while another project depends on it (naming them); an unknown name lists the projects. Subject `project.remove(<project>)`. |

The four that change projects are `exec`: a solution a build system owns changes by running it, which may run the workspace's scripts. Checked: `tools/server-checks/tests/test_project_management.py` (`ProjectToolTests`: a fake model lists, imports this folder and a folder of it, moves, lists templates and removes, the client seeing `solution.changed` with the new kind).

### `ide` (drive the UI for the human)

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-IDE-1 | `ide_open(path, line?, split?)` | meta | Opens a file for the owner (e.g. "here is the bug"). |
| T-IDE-2 | `ide_notify(message, severity?)` | meta | Notification (pushed to the phone if configured). |
| T-IDE-3 | `ide_show_diff(paths?)` | meta | Opens the review diff for the owner. |

### `sandbox`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-SBX-1 | `sandbox_list()` | read | Configured sandboxes and state. |
| T-SBX-2 | `sandbox_run(name, command, timeout_s?)` | exec | Runs inside the sandbox; output streamed to a terminal. |
| T-SBX-3 | `sandbox_task(name, task)` | exec | Runs a toolbar task with `target: sandbox:<name>`. |
| T-SBX-4 | `sandbox_request_network(name, host, port, reason)` | net | Approval card. |

### `profiler`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-PRF-1 | `profile_capture(task?, seconds? or frames?)` | exec | Build + launch under the profiler, capture, stop. |
| T-PRF-2 | `profile_summary(capture)` | read | Duration, frame stats (mean/p95/max), top zones, GC/alloc summary, GPU summary. |
| T-PRF-3 | `profile_top_zones(capture, by: self|total, range?, thread_or_task?, n?)` | read | |
| T-PRF-4 | `profile_frame(capture, index or "worst")` | read | Breakdown of one frame: CPU zones per task, GPU passes. |
| T-PRF-5 | `profile_leaks(capture, group_by?, from_ms?, to_ms?, at_ms?)` | read | Allocations still live at the end (or `at_ms`), only those made between `from_ms` and `to_ms` when given (a level loaded and unloaded); grouped by zone, zone path, call stack, pool, thread or size; each group's first and last allocation and whether it keeps accumulating (a leak) or was made at once (a cache). |
| T-PRF-6 | `profile_compare(a, b)` | read | Zone and frame-time deltas; allocations (in all and per frame), peak memory and collection pauses side by side. |
| T-PRF-7 | `profile_list()` | read | The workspace's captures, newest first, live ones first. |
| T-PRF-8 | `profile_find_zone(capture, name, from_ms?, to_ms?)` | read | One zone's distribution and slowest instances. |
| T-PRF-9 | `profile_flame(capture, from_ms?, to_ms?, depth?, min_percent?)` | read | The call tree merged by name, as indented text; from samples for pprof imports. |
| T-PRF-10 | `profile_messages(capture, severity?, text?, limit?)` | read | The capture's log. |
| T-PRF-11 | `profile_locks(capture, by?, from_ms?, to_ms?, n?)` | read | Lock contention: each lock's waits, contended holds, holds, threads and source; the longest waits with who waited and when. |
| T-PRF-12 | `profile_annotate(capture, from_ms, to_ms?, text)` | write | A note on a time range, kept in the trace file, shown to the owner on the timeline in the AI's color. |
| T-PRF-13 | `profile_allocations(capture, group_by?, by?, from_ms?, to_ms?, n?)` | read | Where a program allocates: per zone (the innermost zone open on the allocating thread), zone path, thread, pool, size class, call stack or function (a stack's innermost frame) - allocations and bytes in all and per frame, mean size, the share freed within the frame they were made in (churn) and what is still live at the range's end; the range's allocations per frame (mean, median, p95, the worst frame) and per second. From allocation samples (pprof heap profiles, the Go client, a C4 collector's sampler) by function or call stack: when the capture has no allocation events, or a C4 capture's counts grouped by call stack or function. |
| T-PRF-14 | `profile_memory(capture, from_ms?, to_ms?, points?)` | read | Memory over time: each pool and memory plot (the GC heap, the Go heap) at evenly spaced points, its growth (bytes per second, in how many steps it rose), its peak and when; the heap left after each collection (a rising floor is a leak); collections - how many, how often, pauses in all and the longest with when, and how many fell in slow frames. |
| T-PRF-15 | `profile_hitches(capture, threshold_ms?, n?)` | read | The slow frames (over `threshold_ms`, else twice the median) and what made each slow: the zones that took longest beyond their usual time a frame (with thread and ref), collections, lock waits, allocations against the usual; then the causes counted over every slow frame. |
| T-PRF-16 | `profile_threads(capture, from_ms?, to_ms?)` | read | Each thread's busy time and share of the range, its longest idle gap, its lock waits and its costliest zones; how many threads were busy at once on average - whether work is serialized on one thread. |
| T-PRF-17 | `profile_zone(capture, ref)` | read | One zone by the `[ref]` other tools print: times, thread, task, frame, text and value, the zones it is in, its children by name, and the allocations, lock waits, messages and collections inside it. |
| T-PRF-18 | `profile_timeline(capture, from_ms, to_ms, depth?, thread?)` | read | What happened in a window, per thread in time order: zones down to `depth` with their times and refs, lock waits, messages, allocations per zone; collections and notes. |
| T-PRF-19 | `profile_diagnose(capture, focus?)` | read | Likely problems ranked by their cost, each with its evidence and the tool call that looks closer: slow frames and their causes, hot zones, work serialized on one thread, lock contention, allocations per frame and churn, memory that keeps growing, allocations never freed, collections, dropped events and warnings. `focus`: all, cpu, frames, memory, locks. |
| T-PRF-20 | `profile_plots(capture, name?, from_ms?, to_ms?, points?)` | read | The program's own counters (plots) over time: each one's min, mean, max and last, and its values at evenly spaced points. |

**As built (2026-10-21, `docs/16` "As built", "Finding problems"):** all twenty are C4 functions of the `ied.profiler` plugin answering through its `profiler.query` server binding, as compact text (durations with units, points in time in milliseconds as `from_ms` takes them, aligned tables). `capture` is a capture id, a `.iedtrace` path or file name, or `latest` (the default: the newest, a live one first). `profile_top_zones` takes `by` (self, total, count, mean, median, p95, max), `group_by` (zone, thread, task), `filter` and a range in milliseconds; `profile_frame` takes a frame number or `"worst"`; `profile_leaks` takes `group_by` (callstack, pool, thread) and `at_ms`; `profile_capture(seconds, project?)` runs the Profile task, records while the program streams, stops the capture and cancels the run (900 s timeout; it fails with the task's output when the program never connects); `profile_locks` takes `by` (wait, max_wait, contended, hold, acquisitions) and a range; `profile_annotate` checks `from_ms` against the capture's length and answers with the note's id, and `profile_summary` lists the busiest locks and every note, the AI's marked as its own. `profile_diagnose` is the first look: it holds the capture to the thresholds `docs/16` lists and answers with findings, each naming the tool that looks closer; `profile_hitches`, `profile_zone` (a `[ref]` any answer prints) and `profile_timeline` explain a slow frame; `profile_memory`, `profile_leaks` (read before a program's teardown when no time is given) and `profile_allocations` (by zone, as live clients send no call stacks; Go's sampled allocations by function) find leaks, churn and collections. The `profiler` skill covers instrumenting each language.

### `gpu`

From `ied.gpu` (`docs/22` "AI"). **Built** (2026-10-21): the tools below marked built, `function` tools of the plugin's C4 code (`plugins/gpu/code/tools.cf`) answering through its `gpu.query` binding from the capture's replay session - the same answers the UI shows - as compact text (`tooltext.cf`, `toolinspect.cf`); the `gpu` skill (`plugins/gpu/skills/gpu`) says how to debug a frame with them. Every tool takes `capture` (an id from `gpu_captures`, default `"latest"`); answers cite EIDs and resources as `[#id]`, marked `rt`, `depth` or `uav` when the event writes them; a refusal (no such capture, a missing `eid`) is the tool's error. Images go only to vision-capable models (the chat's images in tool results).

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-GPU-1 | `gpu_capture(project?, configuration?)` | exec | **Built.** Builds the startup project (its debug configuration's pre-launch task), launches the program for capture as Launch for Capture does, captures the next frame once its capture library answers, stops the program, and returns the new capture's id and events; refused in an untrusted workspace, with why when the program exits or never loads the library. Not yet: several frames. |
| T-GPU-2 | `gpu_events(capture?, filter?, all?, max?)` | read | **Built.** The events in order with EID, kind and name, indented under their markers; `filter` words must all be in the name, `kind:draw` words pick kinds (the UI's filter language without the time terms); `all` adds the state-setting calls; 400 lines unless `max`. GPU times come from `gpu_timing`. |
| T-GPU-3 | `gpu_event_state(capture?, eid, all?)` | read | **Built.** The state tree by stage (a marker: its last action's): shaders, bindings resolved with names, targets, blend/depth/raster, descriptor sets or root signature, push constants; buffers with their offset and layout; `all` adds the descriptor heaps. |
| T-GPU-4 | `gpu_pixel(capture?, eid, resource?, x, y, mip?, slice?, history?)` | read | **Built.** The texel's typed value and raw bits, the texture defaulting to what the event writes, then (unless `history: false`) its Pixel History: each event that wrote it or tried to, with the test that kept a draw out and the values before and after, and the summary sentence. |
| T-GPU-5 | `gpu_debug_shader(capture?, eid, stage: fs|ps|vs|cs, x?, y?, vertex?, instance?, thread?, invocation?, max?)` | read | **Built** for Vulkan (SPIR-V) and D3D12 shaders of Shader Model 5.1 (DXBC): one invocation stepped on the replay GPU (`docs/22` "Shader debugging"), each listing line it ran with the value it made, in order (300 steps unless `max`; DXBC steps name the register, `=> r0.xy = 1, 2`); a pixel with several fragments says how many ran and `invocation` picks one. Not yet: DXIL, source lines. |
| T-GPU-6 | `gpu_scan(capture?, resource?, check: nan|inf|negative)` | read | **Built.** The first action (draw, dispatch, copy, clear, resolve) after which the texture holds bad values, by bisection over the frame (a frame of up to 64 actions whose end is clean is checked action by action), with the count before it; the texture defaults to what the last action writes. Not yet: `range` and an EID window. |
| T-GPU-7 | `gpu_timing(capture?, by: pass|event, top?, repeat?)` | read | **Built.** The frame's GPU time (median of `repeat` replays), by pass (markers, nested) or the `top` costliest actions with primitives, PS invocations, samples passed and CS invocations. Not yet: vendor counters. |
| T-GPU-8 | `gpu_compare(capture?, other, eid?, timing?, repeat?)` | read | **Built**: the events aligned by marker path and call (only here, only in the other, changed calls), at `eid` the state that differs and the output's changed pixels (count, %, largest, box), each pass's GPU time in both (`docs/22` "Compare"); any two captures, D3D12 against Vulkan too. |
| T-GPU-9 | `gpu_texture(capture?, eid, resource?, mip?, slice?)` | read | **Built.** Size, format, each channel's min/max/mean, NaN/Inf/negative counts and where the first NaN or Inf is, and the image (a PNG of at most 1024 on a side under `.ied/local/gpu-captures/ai/`, a float texture brighter than 1 scaled to its brightest); the texture defaults to what the event writes. Not yet: a region, a histogram. |
| T-GPU-10 | `gpu_replay_with_shader(capture?, eid, stage, path? or source?, language?, entry?, profile?, keep?)` | read | **Built**: Edit and Replay (`docs/22` "Edit and Replay"): the stage's shader replaced by a workspace file as the editor holds it, or by source (HLSL; a Vulkan stage's SPIR-V listing), in every pipeline using it; the compiler's diagnostics when it does not go in, else how the output at the event differs from the captured frame; the captured shader put back unless `keep`. Class read: it changes no file, and the replay is put back. |
| T-GPU-11 | `gpu_captures()` | read | **Built.** The workspace's captures, newest first: id, program, API, frame, time, GPU, size, comment. |
| T-GPU-12 | `gpu_resources(capture?, filter?)` | read | **Built.** Id, name, type, format, size, first using EID and use count. |
| T-GPU-13 | `gpu_shader(capture?, eid, stage)` | read | **Built.** A stage's kind (DXBC, DXIL, SPIR-V), bindings, constant buffers or blocks with members and offsets, and up to 300 lines of disassembly. |
| T-GPU-14 | `gpu_buffer(capture?, eid, resource, offset?, size?, as?: float|uint|int|hex)` | read | **Built.** Up to 4 KiB of a buffer after the event, 16 bytes a line. |
| T-GPU-15 | `gpu_limiter(capture?, eid, repeat?)` | read | **Built**: what limits a pass (a marker) or a draw, by experiments on the replay GPU (`docs/22` "Performance"): the range timed as captured and with every draw changed (a trivial pixel shader, a one-pixel scissor, color writes off), the share of its time vertices and primitives, pixel shading, rasterization and depth, and color output take, and the largest named with what helps it; a range under 2 microseconds says so. No vendor counters needed. |

### Plugin and MCP tools

- Plugins add tools via `toolCalls` (a name of the plugin's choosing, `cargo_metadata`; groups come with tool groups). **Built** for tools that run a command (`docs/26` section 8): each turn offers the core tools and the workspace's plugins' tool calls whose `when` holds; the command is the plugin's argv with `${args.<name>}` and `${config:<key>}` put in, its subject `shell(<command>)` for the permission rules, refused in an untrusted workspace; `output: json` gives all of stdout (its log on stderr goes only to Output > AI). Shipped: `nuke_list` (`ied.nuke`), `cargo_metadata` (`ied.rust`). **Built** too (`docs/26` P3): a tool a free function of the plugin's C4 code implements (`function`, `IED::ToolResult <Name>(const IED::ToolCall&)`) or a server binding answers (`binding`, its JSON result the output), run on the plugin's thread with the subject `plugin(<plugin id>/<tool>)` (`docs/11`), refused in an untrusted workspace unless `availableUntrusted`, its card's source the plugin.
- **Images in tool results** (built, 2026-10-21): a function tool's `IED::ToolResult` may list `images` - `IED::ToolImage {mime, path}`, a PNG, JPEG, WebP or GIF file in the call's workspace (its path relative to the root; an absolute path inside the root is taken), at most 4 MB - so a screenshot tool writes the file and names it. The chat keeps the paths with the result (`turns.jsonl`) and reads the files at each request (`plugins/chat/code/images.cf`): to a model that takes images - its profile's `vision` (`ai.models`), or when the profile leaves it out, what its server says (llama.cpp's `/props` `modalities.vision`, asked once per server run) - the step's images go as OpenAI `image_url` parts with `data:` URLs in a user message after the step's results (a tool message carries text only), each captioned with its call and path; to one without, the result's text names each image's path. A request sends the newest 4 images (each costs the model about 1,000 tokens at every step) and names older ones in their results' text. An image that cannot be sent - another type, outside the workspace, missing, too large - is left out with why in the result's text; one removed since is said to be gone. The card shows the images (thumbnails from `GET /w/<wsId>/files/<path>`, open or closed; the segment's `result.images`); compression drops the images of older results first (they count about 1,000 tokens each in the estimate). Not yet: image tools of the core set (`media`), a lightbox.
- MCP server tools are exposed as group `mcp_<server>`; their descriptions come from the server. The UI shows the source on each call card. **Built** (2026-10-26, `docs/04` "MCP"): the `ied.mcp` plugin gives each tool of a workspace's MCP servers as one of its computed `toolCalls` - `mcp_<server>_<tool>`, a `function` tool on its own thread - so the chat offers it like any plugin tool: deferred (a stub in the list, shown whole by `tool_search`) unless the server's `preload` says so, its subject `plugin(ied.mcp/<name>)`, class `exec` or, for a server the owner marks `read`, `read` (offered in Plan mode). A server's `permission` `ask` (the default) is the entry's `approval: "ask"`: every call asks, in `auto` too, until it is allowed for the chat or by an allow rule (`docs/11`). Its result's text, images (shown to a vision model like any tool's), audio and resources come back as the tool's; the card's source is `MCP <server>` (`IED::ToolResult.source`, `docs/26`). A turn first asks the plugin's binding `mcp.ensure` to start the servers whose tools are not known yet; the tools of a server known before stay in the list while it is stopped (`.ied/local/mcp/tools.json`), and a call starts it. A Claude chat gets them in IED's tool list like the rest.
- **Engine tools come from engine plugins**, not the host. For a C4 game engine, the expected set (via its session pipe / MCP server, `docs/17`): `engine_status`, `engine_command(cmd, args)` (driven by the engine CLI's `--describe`), `scene_inspect(path or entity)`, `scene_diff`, `asset_import`, `engine_reload(kinds)`, `engine_capture_frame`, `engine_profile(frames)`. IED's generic tools (`screenshot_window`, `image_compare`, `process_stats`, `profile_*`) cover the rest.

## Mode presets

| Mode | Core | Groups available | Notes |
|---|---|---|---|
| Ask | yes (writes need approval) | all | |
| Plan | read-only core (`fs_read`, `fs_list`, `fs_glob`, `search_text`, `code_*`, `problems_list`, `ask_user`, `tasks_set`) | `code`, `git` (read), `web`, `knowledge`, `agents` (research only), `submit_plan` | no write/exec |
| Agent | yes | all | `finish` required |
| Research sub-agent | read-only core | `code`, `web`, `knowledge` (read) | bounded report |
| Worker sub-agent | core (writes limited to assigned paths) | `code`, `build`, `fs_more` | no git mutation, no spawning |

## Result budgets (defaults)

| Kind | Budget |
|---|---|
| File read | 400 lines or 24 KB per call |
| Search | 100 matches, 2 lines context |
| Shell output | last 200 lines / 16 KB (full output in the terminal and via `shell_read`) |
| Web fetch | 8k tokens (then summary or paging) |
| Diagnostics | 100 items |
| Debug variables | depth 2, 50 children per node |

Budgets scale down automatically for models with small context windows (proportional to the window, floor at 40%).

## As built (M4, first slice, 2026-10-21)

The core set in `plugins/chat/code/`: `fs_read`, `fs_write`, `fs_edit` (exact text, all or none; `\n` in old and new matches a file's `\r\n`), `fs_list`, `fs_glob`, `image_view` (T-MED-1, a workspace image shown to the model), `image_info` (T-MED-4, an image's header), `search_text` (the open documents as edited), `shell_run`, `build_run`, `problems_list`, `program_run`, `tasks_set`, `task_update`, `ask_user`, `finish`, and `docs_search` and `docs_read` when a plugin of the workspace has reference docs. Files are read as the editors have them and written through the document store (an open editor shows the change; the file is saved). Arguments are checked against each tool's JSON Schema (Bedrock's validator); an unknown name is answered with the nearest tools; every result is bounded with how to read on. Writes plan a unified diff for the approval card. `shell_run` runs the command with the system shell in the workspace and shows it with its output in the Output window's "AI" source (the visible AI terminal is to come); it, `build_run` and `program_run` need a trusted workspace. `shell_run` and `build_run` take `background` (2026-10-23, `plugins/chat/code/background.cf`): the task starts and the call answers at once. Why these tools exist, from the language eval (`tools/ai-evals/lang`, 2026-10-22): a model learning C4 read its library's headers through `shell_run` (15 to 25 calls a task) until it had `docs_search` over the toolchain's docs and API digest; it built ad hoc PowerShell timing scripts until `program_run` measured a program; it re-read files to see the line an error named; and it retried an `fs_edit` with the same wrong text until the miss said where it differed.

Not yet: `tools_enable` and most groups (built: `media`, `chats`, `clipboard`, `code`, `git`; "Navigating code").
