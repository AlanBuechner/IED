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
| T-FS-2 | `fs_write(path, content)` | write | Create or overwrite a file (through the document store; checkpointed). |
| T-FS-3 | `fs_edit(path, edits: [{old, new, replace_all?}])` | write | Exact-match replacements, all-or-nothing; whitespace-tolerant fallback with a warning. |
| T-FS-4 | `fs_list(path, depth?, glob?)` | read | Directory listing (respects ignore rules). |
| T-FS-5 | `fs_glob(pattern, root?)` | read | Find files by glob. |
| T-SRCH-1 | `search_text(pattern, regex?, glob?, scope?, context_lines?)` | read | ripgrep-backed workspace search; scopes: project / solution / member / federation. |
| T-SHELL-1 | `shell_run(command, cwd?, timeout_s?, background?)` | exec | Run in the chat's AI terminal; returns exit code + output tail; `background` returns a handle. |
| T-LSP-1 | `code_symbols(path or query)` | read | Outline of a file, or workspace symbol search. |
| T-LSP-2 | `code_definition(symbol or path+line+col)` | read | Definition location(s) + a short excerpt. |
| T-LSP-3 | `code_references(symbol or position, limit?)` | read | References grouped by file with one-line excerpts. |
| T-LSP-4 | `code_read_symbol(symbol)` | read | The full source of one function/type/member - the main way to read code without whole files. |
| T-BLD-1 | `build_run(task?: "build", project?, config?)` | exec | Run a toolbar task; returns status + structured problems (from the Error List), not raw logs. |
| T-BLD-2 | `problems_list(scope?, severity?, source?)` | read | Current Error List (build + LSP + spell) as structured items. |
| T-META-1 | `tasks_set(items)` / `task_update(id, status, note?)` | meta | Chat task list (F-CHT-80). |
| T-META-2 | `ask_user(question, options?, allow_text?)` | meta | Structured question; the run waits for the answer. |
| T-META-3 | `tools_enable(group)` | meta | Enable a tool group for the rest of the turn. |
| T-META-4 | `finish(summary, verification)` | meta | Agent mode only: end the turn with what was done and how it was verified. |

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

### `code` (LSP extras)

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-LSP-10 | `code_hover(symbol or position)` | read | Type/signature/doc info. |
| T-LSP-11 | `code_implementations(symbol)` | read | |
| T-LSP-12 | `code_call_hierarchy(symbol, direction: in|out, depth?)` | read | When supported. |
| T-LSP-13 | `code_diagnostics(path?)` | read | LSP diagnostics for a file or the workspace. |
| T-LSP-14 | `code_rename(symbol, new_name, preview?)` | write | WorkspaceEdit applied through the document store. |
| T-LSP-15 | `code_format(path, range?)` | write | Language formatter. |
| T-LSP-16 | `code_actions(path, line, apply?: id)` | write | List/apply quick fixes for a location. |
| T-LSP-17 | `code_custom(server, method, params)` | read | Call a language plugin's custom LSP request that it marked `aiExposed`. |

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

### `git`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-GIT-1 | `git_status()` | read | Porcelain status, branch, ahead/behind. |
| T-GIT-2 | `git_diff(staged?, paths?, rev_range?)` | read | Bounded diff. |
| T-GIT-3 | `git_log(paths?, n?, rev?)` / `git_show(rev, path?)` / `git_blame(path, range?)` | read | |
| T-GIT-4 | `git_stage(paths or hunks)` / `git_commit(message)` | write | Gated by mode; never pushes. |
| T-GIT-5 | `git_branch(create|switch, name)` / `git_stash(push|pop)` | write | |

### `web`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-WEB-1 | `web_search(query, engines?, n?)` | net | Bundled SearXNG via the egress proxy. |
| T-WEB-2 | `web_fetch(url, mode?: markdown|raw|summary, max_tokens?)` | net | HTML -> clean markdown; PDF -> text; summary uses the `summarizer` role. |

### `browser` (headless Chromium/Edge via CDP, server-side)

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-BRW-1 | `browser_open(url, viewport?)` | net | Returns a page handle; localhost URLs allowed (dev servers). |
| T-BRW-2 | `browser_snapshot(page, mode: text|a11y|dom)` | read | Accessibility-tree snapshot with element refs. |
| T-BRW-3 | `browser_screenshot(page, element?, full_page?)` | read | Image (to vision models) + saved file. |
| T-BRW-4 | `browser_act(page, action: click|type|press|select|scroll|hover, ref or selector, text?)` | net | |
| T-BRW-5 | `browser_console(page, since?)` / `browser_network(page, filter?)` | read | Console messages and failed requests. |
| T-BRW-6 | `browser_eval(page, js)` | exec | Gated (`ask` in default mode). |
| T-BRW-7 | `browser_close(page)` | meta | |

### `media`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-MED-1 | `image_view(path or url)` | read | Sends the image to a vision model; non-vision groups get a text description from the `vision` role. |
| T-MED-2 | `video_frames(path, every_s? or count?, range?)` | read | Extracts frames (bundled ffmpeg or Media Foundation) and sends them as images. |
| T-MED-3 | `image_compare(a, b, threshold?)` | read | Pixel diff: changed-pixel %, bounding boxes, diff image - for visual regression in games/UI. |
| T-MED-4 | `image_info(path)` | read | Dimensions, format, mips, alpha, color space where detectable. |
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
| T-KNW-2 | `skill_read(name, file?)` | read | **Built**: a skill's SKILL.md without its frontmatter, or another file of its folder (`file`, relative to it); an unknown name lists the skills. Enabling the tool groups a skill declares comes with tool groups. |
| T-KNW-3 | `skill_write(name, description, keywords, body)` | write | Approval-gated; writes to `.ied/skills/` or app tier. |
| T-KNW-4 | `history_search(query)` / `history_read(turn or range)` | meta | Retrieve compressed-away chat history. |
| T-KNW-5 | `scratchpad(action: read|write|append|replace_section, section?, text?)` | meta | Chat scratchpad (F-CHT-71). |
| T-KNW-6 | `memory_read()` / `memory_add(fact)` | meta | Project memory (P2, F-AIR-66). |
| T-KNW-7 | `docs_search(query)` | read | Search the workspace's docs folders (paths declared by plugins' system-message sections and settings). |

### `agents`

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-AGT-1 | `spawn_agents(specs[], wait?)` | meta | Research/worker sub-agents (`docs/12`). |
| T-AGT-2 | `agent_send(id, message)` / `agents_wait(ids, timeout_s?)` / `agents_cancel(ids)` | meta | |
| T-AGT-3 | `submit_plan(plan_markdown, tasks)` | meta | Plan mode output. |

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
| T-PRF-5 | `profile_leaks(capture, group_by: callstack|pool)` | read | Live allocations at end. |
| T-PRF-6 | `profile_compare(a, b)` | read | Zone and frame-time deltas. |

### `gpu`

From `ied.gpu` (`docs/22`). Results cite EIDs, which the UI renders as links; images go only to vision-capable models the chat's privacy allows.

| ID | Tool | Class | Notes |
|---|---|---|---|
| T-GPU-1 | `gpu_capture(task?, frames?)` | exec | Build + launch under the active backend, capture N frames, return the capture id. |
| T-GPU-2 | `gpu_events(capture, filter?, range?)` | read | Marker tree slice with EIDs, kinds and GPU times; same filter language as the Event Browser. |
| T-GPU-3 | `gpu_event_state(capture, eid, stages?)` | read | Normalized pipeline state: shaders, bindings (bindless resolved with names), targets, blend/depth/raster. |
| T-GPU-4 | `gpu_pixel(capture, eid, target, x, y)` | read | Typed value plus pixel history with failed tests and a one-line summary. |
| T-GPU-5 | `gpu_debug_shader(capture, eid, target: pixel(x,y)|vertex(i)|thread(xyz))` | read | Runs the shader debugger; returns executed source lines with variable values (bounded). |
| T-GPU-6 | `gpu_scan(capture, target, check: nan|inf|negative|range, eids?)` | read | Bisects the first event that produced bad values; returns EID and pixel count. |
| T-GPU-7 | `gpu_timing(capture, by: event|pass, top?)` | read | GPU times (median of N replays), pipeline statistics, counters when a provider is present. |
| T-GPU-8 | `gpu_compare(a, b)` | read | Aligned events: state diffs, output image diffs (changed-pixel %), timing deltas per pass. |
| T-GPU-9 | `gpu_texture(capture, eid, resource, mip?, region?)` | read | Image for vision models plus stats (min/max/mean, NaN count, histogram). |
| T-GPU-10 | `gpu_replay_with_shader(capture, shader, source)` | exec | Edit and Replay without saving; returns the output diff. |

### Plugin and MCP tools

- Plugins add tools via `toolCalls` (a name of the plugin's choosing, `cargo_metadata`; groups come with tool groups). **Built** for tools that run a command (`docs/26` section 8): each turn offers the core tools and the workspace's plugins' tool calls whose `when` holds; the command is the plugin's argv with `${args.<name>}` and `${config:<key>}` put in, its subject `shell(<command>)` for the permission rules, refused in an untrusted workspace; `output: json` gives all of stdout (its log on stderr goes only to Output > AI). Shipped: `nuke_list` (`ied.nuke`), `cargo_metadata` (`ied.rust`). **Built** too (`docs/26` P3): a tool a free function of the plugin's C4 code implements (`function`, `IED::ToolResult <Name>(const IED::ToolCall&)`) or a server binding answers (`binding`, its JSON result the output), run on the plugin's thread with the subject `plugin(<plugin id>/<tool>)` (`docs/11`), refused in an untrusted workspace unless `availableUntrusted`, its card's source the plugin.
- MCP server tools are exposed as group `mcp_<server>`; their descriptions come from the server. The UI shows the source on each call card.
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

The core set in `plugins/chat/code/`: `fs_read`, `fs_write`, `fs_edit` (exact text, all or none; `\n` in old and new matches a file's `\r\n`), `fs_list`, `fs_glob`, `search_text` (the open documents as edited), `shell_run`, `build_run`, `problems_list`, `tasks_set`, `task_update`, `ask_user`, `finish`. Files are read as the editors have them and written through the document store (an open editor shows the change; the file is saved). Arguments are checked against each tool's JSON Schema (Bedrock's validator); an unknown name is answered with the nearest tools; every result is bounded with how to read on. Writes plan a unified diff for the approval card. `shell_run` runs the command with the system shell in the workspace and shows it with its output in the Output window's "AI" source (the visible AI terminal is to come); it and `build_run` need a trusted workspace.

Not yet: the `code_*` LSP tools, `tools_enable` and every group.
