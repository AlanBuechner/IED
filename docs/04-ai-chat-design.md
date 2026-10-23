# 04 - AI design: models, routing, privacy, context, knowledge, egress

## Purpose

The AI is a first-class citizen of IED. This doc covers the AI runtime's cross-cutting design. Companion docs: `docs/12` (agent loop, modes, sub-agents, stall protection), `docs/13` (tools), `docs/14` (system prompt), `docs/features/07-chat.md` + `08-ai-runtime.md` + `16-orchestration.md` (feature lists).

## Where things live

- **AI runtime (the `ied.chat` plugin's server code, `plugins/chat/code/`, a built-in on the plugin API - `docs/26` "Built-ins on the API"):** providers, dialect adapters, scheduler and routing, agent loop, tool executor, permissions, context ledger and compression, persistence, egress proxy, MCP client, usage tracking, goal queue and verifier.
- **Chat plugin (`plugins/chat/`):** all chat UI (tabs, streaming views, approval cards, right panel, scratchpad, tasks, Mission Control) and the default tool set's bindings.

## Providers

| Provider | Covers | Notes |
|---|---|---|
| OpenAI-compatible Chat Completions | llama.cpp server, vLLM, SGLang, LM Studio, Ollama `/v1`, OpenRouter, others | the baseline; streaming SSE, tools, vision parts |
| Anthropic Messages API | Claude with an API key | thinking, tool use, prompt caching, rate-limit headers |
| Claude Code bridge | Claude through the owner's own installed `claude` CLI | see "Claude" below |
| Plugin providers | anything else | `modelProviders` contribution |

**Model profiles** (app tier) hold: endpoint, model id, context window (discovered), max output, capabilities (tools, vision, thinking, FIM), effort mapping, default sampling, parallel slots (discovered from llama.cpp `/props` where available), privacy class (`local` / `lan` / `cloud`), cost, prompt variant, dialect, stall-policy overrides. Discovery and a capability probe fill most of it ("Test connection").

**Dialects** normalize what local servers actually emit: reasoning fields (`reasoning_content`, `reasoning`, `thinking`, `<think>` tags), tool calls as text (Hermes, Qwen, Mistral, harmony, fenced JSON), parallel call deltas (merge by id then index), finish reasons. Details and the reasons they matter: `docs/12`.

## Model groups and roles

A **model group** assigns, per **role**, a model + temperature + effort + fallback list:

```jsonc
"ai.groups": {
	"plan-claude/impl-local": {
		"primary":        { "model": "qwen3.8-27b@spark", "temperature": 0.3, "effort": "medium", "fallback": ["qwen-coder-32b@desk"] },
		"planning":       { "model": "claude-opus-5-5",   "temperature": 0.3, "effort": "high",   "fallback": ["qwen3.8-27b@spark"] },
		"implementation": { "model": "qwen-coder-32b@desk","temperature": 0.0 },
		"worker":         { "model": "qwen-coder-32b@desk","temperature": 0.0 },
		"research":       { "model": "qwen3.8-27b@spark", "temperature": 0.2 },
		"verifier":       { "model": "qwen3.8-27b@spark", "temperature": 0.0 },
		"summarizer":     { "model": "qwen-small@desk",   "temperature": 0.1 },
		"titler":         { "model": "qwen-small@desk",   "temperature": 0.2 },
		"vision":         { "model": "qwen-vl@spark" },
		"completion":     { "model": "qwen-coder-fim@desk" },
		"inline":         { "model": "qwen-coder-32b@desk", "temperature": 0.1 }
	}
}
```

Groups are defined at app tier; a solution can pick its default group or override roles; each chat selects a group (and effort). A role a group leaves unset is **off** and uses the group's `primary` (a group without `primary` uses the first model in `ai.models`); a new group turns on `primary` alone, so the finer roles stay off until the owner gives one its own model. This is the plan's "Claude plans, a temp-0 local model implements" made concrete (journey J3).

A role's **chain** is its model, then its fallbacks in order. A fallback is a model's name, or `{"model": <name>, "pace": {...}}`
for a Claude model that is used only while the plan's usage is on pace (see Pacing under Claude); the role's own model takes a
`pace` beside it the same way.

**Which role a turn uses** (owner, 2026-10-22: "have the local ai be the primary then have opus as the planning always then have
fallback for the implementation so it would use haiku then fall back to local"): the chat's mode picks it - Ask uses `primary`,
Plan uses `planning`, Agent uses `implementation` - and a role the group leaves off is `primary`. So one chat plans on one chain
and implements on another as it switches modes, and a group without `planning` and `implementation` behaves as one model did
before. The other roles (worker, research, summarizer, titler, ...) wait for the features that use them.

**Effort** is how much a reasoning model thinks before it answers: `low`, `medium`, `high`, `xhigh` or `max` (Claude's five), or unset for what its server does by default. A turn's effort is its chat's, when the chat picked one (the composer's Effort, `chat.set`), else its role's (`ai.groups`). A model profile says what each effort adds to the chat completion request in its `effort` (`{"low": {<fields>}, "medium": {...}, ..., "max": {...}}`, the fields written into the request; a level it leaves out adds nothing, so `{}` never sends one). Without one, a llama.cpp server is sent `chat_template_kwargs: {"reasoning_effort": <effort>}`, which its chat template reads, and any other server OpenAI's top-level `reasoning_effort`: llama.cpp (b10222) ignores the top-level field, and a template that does not read the variable ignores it too. A template that reads only `enable_thinking` (Qwen3's) takes `"effort": {"low": {"chat_template_kwargs": {"enable_thinking": false}}}`. Qwen3.8's template reads both: unset is its `xhigh`, which `high` also means, and an effort it does not know fails the request (400), so a level its server does not take is mapped to one it does (`"max": {"chat_template_kwargs": {"reasoning_effort": "xhigh"}}`). A turn records the effort it used. Measured with `tools/ai-evals` on Qwen3.8 27B at temperature 0: its default (xhigh) took 730 s for the eight questions, `medium` 494 s and `low` 458 s, every answer keeping its facts; medium thinks least per step and keeps reading by symbol, low takes fewer steps by reading whole files.

## A group is a topology: an orchestrator and its sub-agents (2026-10-23)

Owner, 2026-10-23: "model groups are topologys of an orcistrator and sub agents that can call each other. there is no need for a group and model selection they both become one. we should also have the tools and info for the AI to make sure that the sub agents are usfaul and not just makeing things more complex", and "in the chat we should be able to select from eather a grop or an ai directly ... in the menu we should have groups on the left and individual ai's on the right".

**The shape.** A group's `primary` chain is its orchestrator - the chat's own model - with `planning` and `implementation` for Plan and Agent mode as above; `agents` are its sub-agents:

```jsonc
"ai.groups": {
	"claude leads, local hands": {
		"description": "Opus orchestrates; a local coder edits, Sonnet reviews",
		"primary": { "model": "claude-opus", "fallback": ["qwen3.8-27b@lan"] },
		"agents": {
			"coder":    { "model": "qwen-coder@desk", "temperature": 0, "mode": "agent", "calls": ["reviewer"],
			              "description": "Fast local implementer for well-specified edits in a few files" },
			"reviewer": { "model": "claude-sonnet", "mode": "ask", "description": "Reviews a diff for bugs and missed cases" }
		}
	}
}
```

An agent is a role (model, pace, temperature, effort, fallbacks) with `description` (what the orchestrator is told it is for), `mode` (`agent` by default, `ask`, `plan`) and `calls`, the agents it may run itself (none by default; `["*"]` every one). The orchestrator may run every agent of its group. The finer roles of the first design (worker, research, verifier, summarizer, titler, vision, completion, inline) are no longer offered: a group that names them keeps them, unused.

**One choice per chat.** A chat runs on a group or on a single model: `chat.set {group}` clears the chat's model and `{model}` its group (`docs/10`). A single model runs alone, with no sub-agents; when it fails, the default group's chain is tried behind it, as a pinned model's was. The composer's one Model menu shows the groups on the left - each with its orchestrator and its number of agents, the default marked - and the models on the right, by family: Claude's under one heading, each other server's under its host, a family folded unless it holds the chat's choice; a phone shows the two lists one above the other.

**Running a sub-agent.** `agent_run {agent, task, chat?}` starts the agent as a chat of its own - `agent` its name, `parent` the chat that ran it - in the same group, on the agent's chain, in the agent's mode, with its parent's permission; `chat` sends the task to a sub-agent chat that ran before, which keeps what it learned. It is a background task of its parent ("Background tasks" below): the parent goes on, and hears the agent's answer when the agent's turn ends (or waits for it with `background_wait`). A sub-agent runs the agents its `calls` names; chats in a row stop at 3 hops as before, and a chat runs at most 4 agents at once. The Chats list shows a sub-agent's chat under its parent.

**What keeps them useful.**

- A system-message section, for a chat that can run agents: each agent (name, model, local or cloud, what it is for) and when delegating pays - a self-contained, well-specified task that runs while the orchestrator does something else, or that needs another model's strength (cheap and fast for bulk edits, strong for a review) - and when it does not: a task of a few tool calls, one that needs this conversation's context, or the very next step of the orchestrator's own work. A brief says the goal, the files, the constraints, what done looks like and what to return; an answer is checked before it is relied on.
- `agent_list`: the agents with their record in this workspace - runs, how many finished, failed or were stopped, their median time and tokens (`.ied/local/agents.json`) - so the orchestrator sees which pay off.
- `agent_run` answers a brief under 200 characters with a reminder that the agent starts with none of the conversation, and every agent's answer arrives with its time and tokens.

## Scheduling and routing

1. **Role -> candidate list** (the role's model, then its fallbacks).
2. **Filter**: privacy (a private/tainted chat or local-only solution removes `cloud`, and `lan` if the policy says so), capabilities (vision, tools, FIM), context fit (prompt + reserve <= discovered window), availability (not marked exhausted or unreachable).
3. **Pick**: first candidate with a free slot; if none is free, queue on the first candidate (interactive requests ahead of agent work ahead of background work like titles/summaries); optional scorecard preference among equivalents (`docs/features/16`).
4. **On failure** (429/usage exhausted, unreachable, context too small): mark the model's state, move to the next candidate, show a routing note in the chat.

Privacy always wins: a private request waits for a local slot rather than fall back to the cloud.

## Privacy

- **Classes** on models and MCP servers (`local`, `lan`, `cloud`).
- **Per-chat private toggle** (lock icon) and **solution policy** `aiEgress.models: any | local-only | lan-only` in `.ied/solution.json` (committed: a repo can declare itself private).
- **Taint tracking:** `.ied/privacy.json` path globs (and optional detectors for key-like strings) mark content sensitive. When sensitive content enters a chat's context (file read, attachment, tool result), the chat becomes local-only and stays so (sticky), with the reason shown on the lock. Removing taint means forking without that content.
- Private chats also restrict tools: web tools follow the egress policy (tor if configured), `cloud` MCP servers are hidden.

## Claude

IED is primarily local-AI; Claude is optional. Anthropic's policy (Agent SDK overview and legal docs, checked 2026-10-19) says third-party products may not offer claude.ai login or route requests through Free/Pro/Max credentials, and may not collect or intermediate claude.ai credentials; an end user may run the unmodified Claude Code CLI signed in themselves. So:

1. **No Anthropic API provider** (owner, 2026-10-22: "i only care about using the max/pro subscriptions"): Claude runs on the owner's Pro/Max subscription only (`docs/99` #24).
2. **Claude Code bridge** (personal use): IED spawns the owner's own installed `claude` CLI headless (`-p --input-format stream-json --output-format stream-json --verbose --include-partial-messages`), removes its built-in tools (`--tools ""`), exposes IED's tools to it through an MCP server config (`--mcp-config ... --strict-mcp-config`), routes its permission prompts to an IED approval tool (`--permission-prompt-tool`), and maps its stream (assistant/tool/result/`rate_limit_event`) onto IED's chat events. IED never reads, stores or asks for Claude credentials; the owner signs in to the CLI themselves. Subscription usage (5-hour and 7-day utilization, reset times) comes from the CLI's `rate_limit_event` stream and status data. Distributed builds leave it out unless Anthropic approves (`docs/99` #24; "Distributed builds" below).
3. Model ids current at writing: `claude-opus-5-5`, `claude-sonnet-5-5`, `claude-haiku-5-5`, `claude-fable-5-1` (verify at implementation time).

Flags and event shapes must be re-verified against the installed CLI version when implementing (they change across releases).

### The Claude Code provider (built 2026-10-22, checked against Claude Code 2.1.304)

**Profiles.** An `ai.models` entry with `"provider": "claude-code"` is a Claude model the owner's own CLI serves:
`{provider: "claude-code", model: "opus" | "sonnet" | "haiku" | "fable" | a full id, contextWindow?}`. It has no endpoint and
its privacy is always `cloud`. Effort goes to the CLI as `--effort`, each of the five as it is; a chat that picks another one
restarts its session with it (`--resume` keeps the conversation). Which Claude model a turn uses, and when it steps down to a
cheaper one or a local model, is the group's chain (Pacing, below).

**The CLI.** `ai.claude.command` (app tier) is the program and its leading arguments; empty, IED looks for `claude` on PATH, then
`%USERPROFILE%\.local\bin\claude.exe` (the native installer), then the newest copy the Claude desktop app keeps
(`%APPDATA%\Claude\claude-code\<version>\<build>\claude.exe`). IED never reads, stores or asks for Claude credentials: the owner
signs in to the CLI in a terminal of their own (`claude auth login`; not IED's terminal, whose bytes pass through the server), and
IED reads only `claude auth status` (signed in or not, and how) to say so.

**Subscription only** (owner, 2026-10-22: "make sure it uses the subscriptions and not api tokens"). IED runs Claude on the
owner's Claude subscription and never on an API key or another provider's billing:

- Every CLI process IED starts has `ANTHROPIC_API_KEY`, `ANTHROPIC_AUTH_TOKEN`, `ANTHROPIC_BASE_URL`, `CLAUDE_CODE_USE_BEDROCK`,
  `CLAUDE_CODE_USE_VERTEX` and `CLAUDE_CODE_USE_FOUNDRY` taken out of its environment, and reads no settings file (where an
  `apiKeyHelper` could be named).
- Before a session starts, `claude auth status` must say it is signed in with `authMethod` `claude.ai` (a claude.ai login) or
  `oauth_token` (that login's long-lived token, `claude setup-token`); `api_key`, `api_key_helper`, `third_party` and `none` are
  refused, and nothing is sent.
- A session whose `system/init` reports an `apiKeySource` other than `none` is stopped at once.
- A refusal is a routing note and the turn moves to the role's next candidate that is not Claude, or pauses `not_subscription`
  with how to sign in. The status bar's Claude item turns red ("Claude: not on a subscription") and Settings > AI > Claude
  says so with the exact `claude auth login` command for the CLI found.
- A Claude Code session the server itself was started from never reaches the CLI: its variables (`CLAUDE_CODE_SIMPLE` - bare
  mode, which never reads the login - `CLAUDECODE`, the session's ids and sockets) are taken out too.

**Setting it up** (owner, 2026-10-22: "do whatever tooling/automation that is allowed to make the user experience easy";
"i only care about using the max/pro subscriptions"). Everything a person would do in a terminal, IED does in one click,
with the CLI's own windows - IED never sees what is typed into them:

- **Found by itself.** The CLI is looked for on PATH, in the native installer's folder, in WinGet's links
  (`%LOCALAPPDATA%\Microsoft\WinGet\Links`) and in the Claude desktop app, so a machine with the desktop app needs no install.
  The desktop app keeps it in `%APPDATA%\Claude\claude-code\<version>\<build>` when installed with Anthropic's installer;
  the Microsoft Store (MSIX) app's `%APPDATA%` is virtualized, so its copy really lives in
  `%LOCALAPPDATA%\Packages\Claude_<publisher>\LocalCache\Roaming\Claude\claude-code` and a program outside the
  package - IED - does not see it at `%APPDATA%` (a shell the desktop app starts does, which hid this). Both are searched,
  the newest version wins. Until the server has asked the CLI once (`cli.asked`), the page says it is looking rather
  than "not found", and asks at once.
- **Install** (Settings > AI > Claude, when no CLI is found): opens a PowerShell window running the official installer,
  `irm https://claude.ai/install.ps1 | iex`, after the owner confirms the command; the page shows the CLI once it is there.
- **Sign in** (when the CLI is not signed in to a subscription): opens a window running the CLI's own subscription login,
  `claude auth login --claudeai`, which opens the browser at claude.ai; IED asks `auth status` every few seconds (at most 15
  minutes) and the page and status bar change as soon as it says `claude.ai`. The Claude desktop app's own login is the app's:
  the CLI needs its own, and this is it (one click, and an Authorize in the browser where claude.ai is usually signed in).
- **Set up by itself.** The first time the server finds the CLI signed in to a subscription - after Sign in, or a login made
  anywhere else (the keeper asks every 5 minutes while no Claude model is set up) - it adds the Claude profiles
  (`claude-opus`, `claude-sonnet`, `claude-haiku`, `claude-fable`) and a `claude` group whose primary chain steps down as the
  plan runs ahead of pace - `claude-opus` (pace 1.0), `claude-sonnet` (1.25), `claude-haiku` (1.5), then the default group's
  primary when that is not Claude - to the app settings, through the plugin API's `PluginContext::SetSetting` (`docs/26`).
  With no other model, `claude` is the only group, so chats use it at once. It does so once (a mark in the plugin's state
  folder): Claude models the owner removes stay removed; Add Claude models adds them again.

**Distributed builds.** Anthropic's terms (the Agent SDK overview, checked 2026-10-22): "Unless previously approved, Anthropic
does not allow third party developers to offer claude.ai login or rate limits for their products." The owner's own builds
(`Development`, `Debug`) run Claude on their own login; a `Dist` build - what IED ships to others - offers none of this: a
`claude-code` profile is refused with that reason, and Install, Sign in and the automatic setup are off, unless the build
defines `CLAUDE_SUBSCRIPTION_APPROVED` (once Anthropic approves claude.ai login in IED).

**A chat's session.** A chat that uses a Claude profile gets one CLI process, started at its first Claude step and kept between
turns so its prompt cache stays warm; it stops when the chat is deleted, the server stops, or after 10 idle minutes (a
`--resume` brings it back in seconds, and the prompt cache does not outlive that much idling):

    <cli> -p --input-format stream-json --output-format stream-json --verbose --include-partial-messages
          --tools "" --strict-mcp-config --setting-sources "" --disable-slash-commands --permission-prompt-tool stdio
          --allowedTools mcp__ied --system-prompt-file <its claude-system.md> --system-prompt-snapshot off
          --model <id> [--effort <effort>] [--resume <session>]

in the workspace's root, with `CLAUDE_CODE_DISABLE_CLAUDE_MDS`, `_AUTO_MEMORY`, `_BUNDLED_SKILLS`, `_BACKGROUND_TASKS`, `_CRON`,
`_WORKFLOWS`, `_ADVISOR_TOOL` and `_FILE_CHECKPOINTING` set, so the CLI adds nothing of its own (no CLAUDE.md or AGENTS.md reading
of its own, no memory, skills or built-in tools): the system message, tools and context are IED's. The first control request is
`initialize {sdkMcpServers: ["ied"]}`: IED's tools reach Claude as the CLI's in-process MCP server `ied`, whose JSON-RPC travels
as `mcp_message` control requests on the same pipes. `tools/list` answers the chat's tools for its mode with every deferred
tool loaded (the CLI defers MCP tools itself) and `tool_search` left out; the model sees them as `mcp__ied__<name>`. The session's
id (the `system/init` message) is kept in `chat.json`; a session whose system message or effort changed is restarted with
`--resume`, a changed tool list is told with `notifications/tools/list_changed`, a model change is a `set_model` request. The
system message goes in a file, `.ied/local/chats/<chatId>/claude-system.md` (a Windows command line holds 32,767 characters).

**A turn.** IED's loop (`docs/12`) runs unchanged, and a step is one model response:

1. The step sends what the session has not seen of the conversation - the owner's message (its mentions resolved, its images as
   image blocks), harness notes, steering - as stream-json `user` messages. A session that does not have the chat's earlier turns
   (a new one for a chat another model answered so far, or one `--resume` could not find) first gets them written out as text.
   Nothing is sent while a query runs (between a step's tool calls and the next response): the CLI would make it a query of its
   own after this one. Steering that comes then goes with the next tool result's text, or as a message once the query ends,
   and a turn does not finish before Claude has it. A retried step with nothing new (the CLI's query ended with an error, or
   the CLI was restarted) tells Claude to go on.
2. It reads the CLI's output: `stream_event`s (the Messages API's raw events) stream thinking, text and tool-use input into the
   segments as any model's; `message_stop` ends the step with the response's stop reason (`tool_use`, `end_turn`, `max_tokens`,
   `refusal`) and its usage. Its `tool_use` blocks are the step's calls.
3. Each call runs through IED's own path - permission rules, approval cards, the tool card - and its result answers the CLI's
   `tools/call` for it (matched by `_meta["claudecode/toolUseId"]`) as MCP content (text, images as image blocks). The next step
   reads the CLI's next response.
4. A turn that ends (an answer in Ask or Plan mode, `finish` in Agent mode, a pause) reads on to the CLI's `result`; Stop sends
   `interrupt`. The turn's messages are kept in IED's own format too, so the chat can move to a local model and back.
5. The CLI's own errors map onto the stop taxonomy (`docs/12`): an assistant message with `error` `rate_limit` or
   `billing_error` is a usage limit - the model is marked exhausted until the reset its window names and the turn moves to the
   role's next candidate (a fallback; a local model), or pauses saying when Claude resets; `authentication_failed` pauses with how
   to sign in; `overloaded` and `server_error` are transport errors, retried and then sent to the fallback; `system/api_retry`
   shows as a routing note. The CLI compacts its own context, so IED's compression does not run on a Claude step; the window is
   the one the CLI reports (`modelUsage.<model>.contextWindow`).

**Usage.** The server keeps the account's plan usage: each window's utilization (0-100) and reset - `five_hour` (the session),
`seven_day` (the week), the per-model weekly windows (`seven_day_opus`, `seven_day_sonnet`, `model_scoped` by display name),
extra usage - from the CLI's `get_usage {skip_behaviors: true}` control request, asked through a running session or a short-lived
CLI process of its own (no model request, no usage spent): when a client asks (`ai.usage {refresh: true}`), after each Claude
turn, and every `ai.claude.usageRefreshMinutes` (default 10; 0 never) while a `claude-code` profile is set up. Between, each
`rate_limit_event` a session streams updates the windows it names. It is on the app-wide topic `aiusage` (`docs/10`).

**Pacing.** A window of length L (5 h, 7 d) at utilization u resetting at R is `e = 1 - (R - now) / L` through, and its pace is
`p = u / (100 e)`: 1.0 is the even line that runs out exactly at the reset, 2.0 is spending twice as fast. Early in a window
(under 5% through and under 5% used) its pace counts as 0.

Pacing is part of a group's chains (owner, 2026-10-22: "the stretch it over the week should use the fallback models in the
group ... there are lots of other ways to build out the topology that we should give the user control over"). Any Claude model
in a role's chain - its model or a fallback - may have a pace:

    "ai.groups": {
      "local, claude plans": {
        "primary":        { "model": "qwen3.8-27b@lan" },
        "planning":       { "model": "claude-opus" },
        "implementation": { "model": "claude-haiku", "pace": { "max": 1.0, "target": "week" },
                            "fallback": ["qwen3.8-27b@lan"] }
      },
      "claude": {
        "primary": { "model": "claude-opus", "pace": { "max": 1.0 },
                     "fallback": [{ "model": "claude-sonnet", "pace": { "max": 1.25 } },
                                  { "model": "claude-haiku", "pace": { "max": 1.5 } }, "qwen3.8-27b@lan"] }
      }
    }

A turn skips a paced Claude model while its pace against `target` is over `max`, and goes on down the chain - to a cheaper
Claude model or a local one - with a routing note ("claude-opus skipped for pace: the week is at 1.85x pace, over its 1.00x";
"left out" for one used up). Only the places ahead of the model the turn runs on get a note: those behind it were never
reached. The turn's header names that model from its start (`turn.start`), and the next one's after a failover (`turn.stats`). `target`
is `session` (spend each 5-hour window fully: `five_hour`), `week` (stretch the week: `seven_day` and the model's own weekly
window) or `both` (the default: the higher). A Claude model without a pace is used whatever the pace - "Opus for planning,
always" - and only a used-up window leaves it out (100%, or a `rejected` rate-limit status; a model's own weekly window for that
model). Models skipped for pace stay at the end of the chain as a last resort, the least over its pace first (pace over
`max`: with the week at 1.85x, Haiku at 1.5 comes before Sonnet at 1.25 and Opus at 1.0): a turn whose every other candidate
fails (a local server down) still gets one, and the cheapest that fits. A pace on a model that is not Claude does nothing; a pinned model
(`chat.set {model}`) is the owner's choice and is never skipped for pace. The choice holds for the whole turn (a model change
mid-turn would read the whole context into a new cache).

The first design (also 2026-10-22) hid a ladder inside one profile, `model: "auto"` with `pacing: {ladder, target}`; the ladder
is the `claude` chain above now, and the owner edits it like any other. An `auto` profile in the app settings is rewritten once:
the profile goes, each role that named it gets its ladder as paced places (1.0, 1.25, 1.5 down the ladder, against its target),
and a `claude-<model>` profile is added for a rung that had none.

**Switching models.** A chat may pin a model of `ai.models` over its group's primary (`chat.set {model}`, the composer's Model
menu, `""` back to the group's): the group's fallbacks stay behind it, so a turn on the pin notes none of them, and one tries
them only when the pin fails. The Model menu's label is the pin ("Group's" without one), and the model chip under the composer
the model the next turn starts on: both follow `chat.changed`, which carries that model on the chat's topic (`docs/10`
"Chats"), whoever pinned it - the owner here or on another device, or the AI's `chat_set_model`. The AI has two tools (group `models`, `docs/13`):
`ai_usage` (the plan's windows with their pace and resets, and for each paced chain which model a turn uses now and why) and
`chat_set_model` (pin a model for this chat's next turns or another chat's, an Exec call: in Ask mode the owner approves it).

**In the UI.** The status bar's Claude item (`Claude 5h 42% wk 61%`; yellow when a window runs ahead of pace or the CLI warns,
red when one is used up; resting the pointer on it shows a card drawn as the settings page draws the usage (owner, 2026-10-22:
"a more visual representation of the usage limits and pace"): each window's bar with the even line, its pace - yellow ahead
of it - and reset, what holds Claude back, and each paced chain with the model a turn uses now and why the ones before it are
passed over; `usageHoverCard` in `plugins/chat/ui/claudeusage.js`, on `ui-kit/hovercard.js`) opens Settings > AI > Claude: the CLI (where it is, its
version, whether it is signed in), each window as a bar with the even line marked, its pace and reset, each paced chain with
every place's state (used now, over its pace, used up) and the model a turn uses now, Refresh, and Add Claude models (the
automatic setup's profiles and `claude` group). Settings > AI > Model Groups edits the chains: a role's model and its pace, the
fallbacks in order (moved up, removed) each with its own pace - always, or at most 0.75x to 2x against the session, the week or
both - and says which mode uses each role. The command Chat: Claude Usage opens the Claude page (on a phone, which has no status
bar, from the command palette).

**As built.** Server (`plugins/chat/code/`): `claudewire` (usage windows from `get_usage` and `rate_limit_event`, a model's pace
against a target, the setup's chain and the one-time rewrite of `auto` profiles, a response from stream events, the CLI's
errors, the lines IED writes, the history a new session is told), `pacing` (each place of a chain now - in use, over its pace,
used up - the order a turn tries them, and the chains `aiusage` and `ai_usage` show),
`claudecli` (finding the CLI, a running one with its reader thread and control requests, the environment and arguments,
`auth status`), `claudesession` (a chat's session: start and resume, IED's MCP server and permission answers, `tools/call`
kept for its run, `set_model`, settling a query), `claudeaccount` (the usage record, `aiusage`, `ai.usage`), `claudechats` (the
sessions by chat, the keeper thread: the periodic usage ask, idle sessions stopped), `runclaude` (the step, the tool answers,
the turn's end, usage limits and refusals routed, the chain ordered by pace), `modeltools` (`ai_usage`, `chat_set_model`);
`models` reads `provider`, a role's chain with its paces and the role a mode uses, a chat's `pinnedModel` and its session
(`claude: {session, turns}`) are kept in `chat.json`. Client (`plugins/chat/ui/`): `claudeusage.js` (the status item, Settings >
AI > Claude with its Pacing card, Add Claude models), `groupsettings.js` and `pacing.js` (Model Groups: each role's mode, a
Claude model's pace, fallbacks moved earlier), the composer's Model menu. Tests: unit `server.ai.claude`
(`claudewire_tests.cf`, 16) and `server.ai.models` (`ai_tests.cf`), server checks `test_claude.py` (22, against `tools/fake-claude`, a fake
CLI that speaks the same protocol and records what IED sent), frontend `claude.test.js`. Checked by hand against the real CLI
(2.1.304, the Claude desktop app's copy, not signed in): discovery, `--version`, `auth status`, `get_usage`, IED's exact session
arguments with the MCP handshake, and the refusal before anything is sent. A signed-in turn was checked against the fake only:
the owner signs in (`claude auth login`) for the first real one.

## Context management

- **Ledger:** every context item typed and counted (provider usage is authoritative; per-family tokenizers or llama.cpp `/tokenize` for estimates). Feeds the right-panel context view.
- **Tiered compression** at a high-water mark (defaults: warn 70%, compress at 85% of the discovered window, target 50%):
  1. *Mechanical:* superseded file reads dropped (a later read of the same file wins), large tool results trimmed to conclusions + excerpts, stale diagnostics dropped. As built: at a compression a read the same range of the file was read again after becomes "[superseded: <path> was read again later]", and one the file was written or edited after becomes "[out of date: ...; read it again if you need it]" (`plugins/chat/code/superseded.cf`); then older results are trimmed and whole earlier turns left out.
  2. *Summary:* older turns summarized by the `summarizer` role into a structured summary (goal, constraints, decisions, progress, files touched, open questions, exact snippets to keep).
- **Cache-aware:** the system message and early prefix stay byte-identical; between compressions the context is append-only; compression happens in big steps rarely, so llama.cpp and Anthropic prefix caches stay warm (this is what makes long local chats *fast*, not just possible).
- **Never lost:** the raw history stays on disk; `history_search` / `history_read` retrieve compressed-away details. Scratchpad and task list are never compressed (they travel as tail notes).
- **Overflow recovery:** a context-length error updates the model's window from the error text, compresses and retries (`docs/12`).
- **The meter as built (2026-10-23; owner: "the context display in the chat window is not always acuret").** After each step the chat's `context.changed` (kept in `chat.json`, so a restart shows it) says how full the context is: the tokens the model's server counted for the step's prompt plus the reply it gave, against the window of the model that counted them (`model`). A step that failed before an answer leaves it as it was (it used to drop to 0%). The window is the profile's `contextWindow`, else what the server, the CLI's `modelUsage` (the turn's model's entry) or a context error reported, kept in the chat plugin's `windows.json` so the next server run knows Claude's before its first query ends; a server that did not answer discovery is asked again after a minute, and a server listing several models gives the window of the profile's model. Agent mode's last Claude step after `finish` is counted too. The parts (system prompt, tool definitions - every tool for Claude - messages with their thinking and images, tool results) are IED's estimates scaled to add up to the count; `compressAt` (85) is where IED compresses a local model, which counts from its estimate calibrated by the last real count, so the meter and compression agree; Claude's says it compacts its own (`compacts`). A chat that switches models while idle is redrawn for the new model's window. The meter goes to `chat.json` at every step, not only when the turn ends (a server stopped in between kept the turn start's header, without it), with the turns compression left out (`compressedTurns`), which a restart keeps out of requests and estimates. A chat with turns always has a meter (owner, 2026-10-23: "if the chat has stuff in it then we should ALWAYS show the context usage"): one no model counted yet - an older server ran it, or its count was lost - is estimated when it is first shown from what its next turn would send (its system message, tool definitions, messages and tool results, for the model it would start on) and marked `estimated`, shown with a "~" until the next reply counts it. `plugins/chat/code/contextmeter.cf`, `run.cf` (`EstimatedContext`), tests `server.ai.contextmeter`, `server.ai.probe`, server checks `test_chat_context.py`.

## System message

Assembled in a fixed order - base prompt, style rules, mode instructions, solution `AGENTS.md`, bounded plugin sections, workspace summary, tool-group index, skills index - with volatile state kept out of it. Exact rules and the shipped texts: `docs/14`, `plugins/chat/prompts/`. Nested `AGENTS.md` files (subfolders, federation members) are injected as tail notes the first time the agent works in that area.

## Skills

- Sources: `.ied/skills/` (project, committed), `%APPDATA%/IED/skills/`, plugin `skills` paths. Format: `shared/host-api/README.md` (compatible with the C4 skills the toolchain's plugin ships, `C4/IedPlugin/skills/`, which use `triggers`).
- Index: BM25 over name, description, keywords, triggers and headings; optional embedding rerank (P2).
- In the prompt: the whole index when small (<= 40), otherwise the top 15 for the task + `skill_search`. Bodies load only via `skill_read`; a skill can declare tool groups to enable.
- Authoring: `skill_write` (approval-gated) and "synthesize skill from this chat"; the orchestration learning loop proposes skill updates after successful goals.

## MCP

MCP client for stdio and streamable-HTTP servers (Blender, a game engine, anything), configured at app/solution/project tier (project-defined servers need trust). Tools surface as group `mcp_<server>` in the same registry; resources become @-mentionable context; prompts become slash commands. Each server has a privacy class and inherits the workspace egress policy. IED is also an MCP *server* (P1) so external agents can use its LSP/build/editor tools.

## Web egress and secure mode

- **SearXNG bundled** (`docs/21`); `web_search` goes through it. As built (`docs/29`): run by the `ied.websearch` plugin, not the host, its engines asked through Tor (the `ied.tor` plugin) - Tor carries web search only for now (owner, 2026-10-21); `web_fetch` (`ied.webfetch`) goes direct, looking like a browser. The egress proxy below is not built.
- **Egress proxy:** all AI-originated HTTP (fetch, search upstreams, browser tool, MCP HTTP) goes through the host's proxy with a per-workspace policy `direct | tor | proxy:<profile>`; untrusted workspaces have egress off.
- **tor** bundled (managed process, new circuit on rate limiting); **VPN profiles** are SOCKS5/HTTP proxy endpoints (a provider's proxy, or a `gluetun` container started via the sandbox plugin for WireGuard/OpenVPN providers).

## Persistence

Per chat, under `.ied/local/chats/<chatId>/` - the workspace's local state, `docs/07` (generic chats under `%APPDATA%/IED/chats/`):

| File | Content |
|---|---|
| `chat.json` | header: title, mode, group, permissions, privacy/taint state, task list, scratchpad, pinned items, the clipboard |
| `turns.jsonl` | completed turns and segments (the transcript) - append-only, compacted occasionally |
| `steps.jsonl` | run journal (state transitions, tool call started/finished) for crash-safe resume |
| `trajectory/*.jsonl` | raw requests/responses/timings, rotated with retention limits |
| `media/` | attachments and tool media as files (never base64 in JSONL) |
| `checkpoints/` | content-addressed pre-edit file snapshots for rewind |

Only the active window of each chat stays in server memory; older turns load lazily on scroll.

## Deleting a chat (built, 2026-10-22)

Deleting a chat keeps it (owner, 2026-10-22: "it will get archived for 30 days before it gets fully deleted that way you can recover it later"). `chat.delete` moves the chat's folder whole to `chats-deleted/<chatId>/` beside `chats/` in the local state, with `deleted.json {deletedMs}`; the chat leaves the workspace's chats (its topic and Claude Code session go) and is listed under `deleted` on `chats:<wsId>` with `purgeMs`, when it goes for good. `chat.restore` moves it back and loads it as it was - turns, tasks, queue, allowed rules, its Claude session id - and `chat.purge` removes it at once. The server purges a chat deleted `ai.chats.keepDeletedDays` (30 by default, app or solution tier) ago when its workspace opens and every hour after (`plugins/chat/code/deletedchats.cf`). A running chat cannot be deleted (`conflict`): stop it first.

In the Chats list (`docs/15` "Chat activity") Delete needs no confirmation - the toast offers Undo, and the Deleted section (folded by default) lists each deleted chat with the days it is still kept, Restore (which opens it) and Delete Permanently (confirmed).

## Chats talk to each other (built, 2026-10-21)

A chat can hand work to another chat of its workspace and hear back, the way a lead hands a task to a colleague. The tools are the deferred group `chats` (`docs/13`), loaded with `tool_search` or by calling one:

- `chat_list` names the workspace's chats (id, title, mode, running or idle, the first line of its last answer); `chat_read` shows another chat's last turns (at most 5), messages and answers without the tool calls.
- `chat_send` sends a chat a message: an idle chat starts a turn with it, a running one gets it at its next step (like a steering message). `chat_create` starts a new chat with a message (a title and mode optional; it uses this chat's model group), to split the work. Both are Exec tools: they ask in Ask mode, and "Allow for this chat" adds `chat.send(<id>)` / `chat.create(*)`.
- `chat_wait` waits for another chat's turn to end and returns its answer (default 120 s, at most 600 s; a stop ends the wait).

The receiving turn says who sent it: its user message carries `from {chatId, title}`, the transcript heads it "From chat <title>" with a link that opens that chat, and the model is told the message came from that chat and that it can answer with `chat_send`. A chain of chats messaging chats is cut at 3 hops (`hops` on the turn), so two chats cannot keep each other running forever.

## Background tasks (2026-10-23)

Owner, 2026-10-23: "ai background tasks that the ai can be notified of when they finish as well as be able to check on there status".

- `shell_run` and `build_run` with `background: true`, and `agent_run`, start a task and answer at once with its id (`bg-<n>`, the chat's counter); the turn goes on. A command's output goes to Output > AI as before.
- `background_status {id?}` lists the chat's tasks (state, title, time, exit code, the first line of how it ended), or one with its output's end; `background_wait {id?, seconds?}` waits for that task, or any running one, to end (120 s by default, 600 at most; a stop ends the wait); `background_stop {id}` stops one. They are the deferred group `background`.
- **Told when they end.** A running turn gets each ended task at its next step as a harness note (`background_ended`), not as the owner's steering; a turn that would finish with an ended task not yet told goes on to read it. An idle chat starts a turn by itself whose message names the tasks that ended (the transcript heads it "Background tasks"); a paused or waiting chat hears them with its next turn.
- **Lifetime.** A task outlives the turn that started it; Stop on the chat stops its turn, not its tasks. Deleting a chat stops them. `chat.json` keeps the list (id, kind, title, state, times, exit code, summary); a task still running when the server stops is `lost` when it starts again, its process gone with the server.
- **In the UI.** The side panel's "Background tasks" section lists them - a spinner while running, how each ended, its time - with Stop on a running one and its output's end unfolding under it; the chat topic's `background.changed {tasks}` carries the list (`docs/10`).

## What a chat has open (built, 2026-10-21)

Other plugins open things on a chat's behalf - the Browser plugin's pages an AI drives (`docs/27`), and later terminals and processes. A plugin says so through the chat plugin's server binding `chat.resource` (`{chatId, ref, title?, kind?, open?}`; `open: false` takes it off): `ref` is the tab that shows it (`browser:pg-3`). The list is what is open now, kept for the server run only (after a restart nothing is loaded, and a plugin reports a tab again when it loads it); the chat's topic says when it changes (`resources.changed`) and the side panel lists it under "Open by this chat", each row opening its tab, so the owner can watch the AI work in the page.

## The AI's clipboard (built, 2026-10-21)

A chat copies text it has seen - lines of a file, of an earlier tool result, or text it gives - to a named clip with `clipboard_copy`, and pastes it into any later call by writing `{{clip:<name>}}` in a string of the call's arguments: a long block matched exactly as `fs_edit`'s `old`, a function moved to another file, an error message reused in a search, without the model typing it again (`docs/13` `clipboard`). The clips are the chat's own, kept in `chat.json`, and the side panel lists them under "Clipboard" (name, where it came from, its lines), each unfolding to its text, so the owner sees what a call pasted.

## Attachments and @-mentions

Drag-drop, paste and picker attachments (images/videos to vision-capable models; others become file references read on demand). `@` opens fuzzy suggestions: files and folders, symbols (`@#Name`), open documents, terminals, output sources, Error List, `@diff`, other chats; mentions become structured references resolved at call time.

**Design (2026-10-22), built.** What the owner puts in a message besides its words, and how the model gets it:

- **Mentions.** Typing `@` at a word's start in the composer (or its `@` button) opens a list above the text box that narrows
  as more is typed; Up/Down choose, Enter or Tab inserts, Esc closes. The list is the client's, from methods it already has: files
  (`fs.files`, open documents first) and their folders (`@src/`), symbols for `@#` (`lsp.workspaceSymbols`), terminals
  (`term.list`), output sources (`out.sources`), the workspace's other chats, and two fixed entries, `@problems` (the
  Error List) and `@diff` (the working tree's changes). Picking one writes `@<label>` into the text - `@src/ring.cf`,
  `@#Game::Ring::Push`, `@terminal:Build`, `@output:Git`, `@chat:Fix the build` - and the composer keeps it as a
  structured reference, `{kind, ref, label}` (`kind` one of `file`, `folder`, `symbol`, `terminal`, `output`,
  `problems`, `diff`, `chat`; `ref` the path, the symbol's place `path:line` (1-based, its label's name narrowing
  it; a bare name works too), the terminal or source id, or the chat id). A mention whose `@<label>` is no
  longer in the text when it is sent is dropped.
- **Attachments.** The paper clip (a file picker), a paste of files or an image, or files dropped on the composer. Each
  is uploaded at once (`PUT /api/upload`, `docs/10`) and shows as a chip above the text box - an image's picture, any
  other file's icon, name and size - with a button that takes it off. A workspace file dragged from the Solution
  Explorer is a mention of it, not an attachment.
- **The message** goes as `chat.send {..., mentions: [{kind, ref, label}], attachments: [{blobId, name}]}` (`docs/10`).
  The server copies each attachment out of its blob into the chat's `media/` folder (`.ied/local/chats/<chatId>/media/
  m<n>-<name>`, `n` the chat's counter and the name's last part with anything but letters, digits, `.`, `-` and `_`
  made `_`; at most 16 per message and 20 MiB each; an expired blob is refused with `not_found`), and the
  turn's user message keeps `attachments: [{name, path, mime, bytes}]` (the path workspace-relative) and `mentions`.
- **Resolved when the turn starts** - "at call time": the text the model gets for each mention is read when the turn's
  first request is built, not when it was typed, and appended to the user's message as one block per mention,
  `<mention label="@src/ring.cf" kind="file">...</mention>`. Most are what the read tool for them answers, so the model
  sees a mention as it would see that tool's result (`docs/13`):
  - a file: `fs_read` - the text the document store has (an open document's unsaved edits included), its first 400
    lines, and how to read on;
  - a folder: `fs_list`;
  - a symbol: `code_read_symbol`, its declaration with its doc comment;
  - `@problems`: `problems_list`, the workspace's Error List;
  - an output source: its last 200 lines (the new `IED::OutputTail`);
  - a terminal: its last 200 lines without escape sequences, from the terminal plugin's binding `terminal.text`
    (plugins compose through bindings, `AGENTS.md` rule 7);
  - `@diff`: each of the workspace's repositories' `git diff HEAD` and the files it does not track (IED's
    `.ied/local/` left out), at most 64 KiB, from the git plugin's binding `git.workingDiff`;
  - a chat: `chat_read` of its last 3 turns.
  A mention that cannot be read (a file deleted since, a chat gone) is a block that says why. The resolved message is
  the turn's stored user message, so a retry, a restart and the transcript show what the model saw; later steps of the
  turn do not read the mentions again.
- **Attachments in the request:** an image (PNG, JPEG, GIF, WebP, at most 4 MiB) goes to a model that takes images as an
  image part of the owner's own message (its text first), counted with a tool result's images among the newest 4 a
  request sends (`docs/13` "Images in tool results"); to a model that
  does not, and for any other file, the message names it - `[attached: .ied/local/chats/<id>/media/m12-notes.txt
  (notes.txt, 12 KB, text/plain); fs_read reads it]` - and the model reads it with `fs_read` when it needs it (the path
  names the file in the workspace's local state wherever that is, `docs/07`).
- **The transcript** shows a user message's mentions and attachments as chips under its text; an image chip shows the
  picture (the workspace's images route, `docs/10`), a file chip opens the file, a mention chip opens what it names.

New in the plugin API for it (`docs/26`): `IED::ReadBlob(id, contentType, bytes, problem)` (an upload's bytes) and
`IED::OutputTail(wsId, sourceId, text, problem)` (the text an output source keeps); new bindings: the terminal
plugin's `terminal.text` and the git plugin's `git.workingDiff`, which the chat's manifest consumes.

**As built.** Server: `plugins/chat/code/sent` (chat.send's mentions checked, attachments copied), `mentions` (each
mention read at turn start, `TurnRun::ResolveMessage`), `images` (`WriteUserWithImages`); a queued message keeps its
mentions and attachments in `chat.json`; `plugins/terminal/code/terminaltext`, `plugins/git/code/workingdiff`. Client:
`plugins/chat/ui/mentions.js` (the rows: `fs.files`, `lsp.workspaceSymbols` after `@#`, `term.list`, `out.sources`,
`chat.list`), `mentionlist.js` (the list), `attachbar.js` (uploads and their chips), `userchips.js` (the transcript's
chips). Tests: `server.ai.mentions`, `server.terminal.text`, `server.git.workingdiff`, `tools/server-checks/tests/
test_chat_mentions.py`, `frontend/tests/chatmentions.test.js`, the e2e scenario `chat_mentions_and_attachments`. A
queued message keeps what it names and carries through the queue row's Edit (`chat.unqueue` answers it whole; its files
go again by their media path) and Send it now (`chat.unqueue {start: true}` starts it on the server).

## As built (M4, first slice, 2026-10-21)

The runtime is the `ied.chat` plugin's server code (`plugins/chat/code/`, compiled into the server and written against `src/api/` like any plugin's, since P4 of `docs/26`; before, `src/ai/`); the plugin's UI module is its UI and its manifest declares its settings and the AI output source:

| Part | Files | Does |
|---|---|---|
| Wire | `wire/sse`, `wire/request`, `wire/completion`, `wire/dialect` | SSE events and comments; streaming requests (a POST, or a GET) over Bedrock `Net::`/`Http::`; a completion put together from chunks (reasoning fields, calls merged by id then index, usage and llama.cpp timings, errors in the stream, malformed chunks counted and skipped); `<think>` blocks split from content across chunks and calls written as text in six dialects |
| Step | `step` | one request under the watchdogs, text released with a held tail so a call's markup never shows as prose |
| Classification | `classify` | the stop taxonomy of `docs/12`, `announced_no_action` from the reply's last sentence |
| Models | `models`, `probe` | profiles (`ai.models`) and a group's role candidates (`ai.groups`): the role's model, then its fallbacks, an off role the primary; what a model's server says of itself |
| Permissions | `permissions` | `ai.permissions` rules and the chat's mode (`docs/11`) |
| Tools | `tools/` | the core toolset (`docs/13` "As built") |
| Prompts | `prompts` | the system message from `plugins/chat/prompts/` (base, style, mode, the workspace's `AGENTS.md`, a workspace summary) and the harness notes by name |
| Run | `run`, `runstream`, `runtools` | the loop of `docs/12` on a thread per turn |
| Chats | `chat`, `chatstore`, `service` | the model, persistence and `chat.*` (`docs/10` "Chats") |

**Settings** (declared by `plugins/chat/settings.schema.json`):

| Key | Tiers | Value |
|---|---|---|
| `ai.models` | app | `{<name>: {endpoint, model?, contextWindow?, maxOutput?, privacy?, vision?, keepThinking?, effort?: {low?, medium?, high?, xhigh?, max?}, apiKey?: {env}, watchdog?: {connectS, prefillBaseS, prefillRate, idleS, stepCapS}}}`: an OpenAI-compatible endpoint (`http://host:port/v1`); `effort` the request fields each effort adds ("Model groups and roles"); `vision` says whether the images tool results show are sent to it (`docs/13` "Images in tool results"; left out, its server says: llama.cpp's `/props` `modalities.vision`); `keepThinking` (default true) sends a llama.cpp server the turn's earlier thinking with each step as `reasoning_content`, as its chat template expects - without it the template shows an empty think block for every earlier step and the model works its plan out again (measured in `tools/ai-evals/lang`, variant v10); a key is only ever named by its environment variable. Or `{provider: "claude-code", model?, pacing?: {ladder, target}, contextWindow?, watchdog?}`: Claude through the owner's Claude Code CLI on their subscription ("The Claude Code provider") |
| `ai.claude.command` | app | the Claude Code CLI's program and leading arguments; `[]`, the default: found (PATH, the native installer's, the Claude desktop app's) |
| `ai.claude.usageRefreshMinutes` | app | how often the plan's usage is asked while a `claude-code` profile is set up (10; 0: only when asked and after Claude turns) |
| `ai.groups` | app | `{<group>: {description?, <role>: {model, temperature?, effort?, fallback?: [names]}, agents?: {<name>: {model, description?, mode?, calls?, temperature?, effort?, fallback?}}}}`; a turn uses its mode's role, with the chat's effort when it picked one; `agents` are the orchestrator's sub-agents ("A group is a topology") |
| `ai.defaultGroup` | app, solution | the group of a chat that names none; empty: the first group; with no group at all, the first model |
| `ai.permissions` | app | `{allow, confirm, deny}`: lists of `kind(pattern)` rules |
| `ai.maxSteps` | app, solution | model requests per turn before it pauses; 0, the default, is no limit |
| `ai.tools.loadAll` | app, solution | send every tool's definition with every request; off, the default, plugin tools and tool groups are listed by name and loaded on demand (`tool_search`, `docs/13`) |

**Persistence:** per chat under the workspace's `.ied/local/chats/<chatId>/` (its local state, `docs/07`: a solution's folder, a folder's app data, so chatting writes nothing into an opened folder; chats an older server left in a plain folder move there when it opens): `chat.json` (title, mode, permission, group, tasks, queued messages, the rules allowed for the chat, the files the AI changed), `turns.jsonl` (each finished turn with the messages the model was sent) and `running.json` (the turn in flight, written at every step and tool boundary). A `running.json` found at start is a turn a restart interrupted: it ends `interrupted`, a call it had running gets a result saying its effect is unknown, and the chat waits paused for the next message.

**Discovery and settings:** `ai.probe` asks a model's server what it serves (`docs/10`), and llama.cpp's `/props` whether it takes images (`vision`, its `modalities`); a turn asks each profile's server once per server run: a profile without `contextWindow` uses the slot context it reports, a profile without `vision` takes what the server says of images, and a llama.cpp server streams the prompt read so far (`docs/12` "Watchdogs"). Settings > AI (`docs/07` "The settings window as built"): **Models** lists the profiles with Test connection and a form to add or change one (written to the User tier; "Images tool results show" is `vision`: as its server says - left out -, send them, or never); **Model Groups** shows the groups (the default starred; New, Duplicate, Rename, Make default, Delete) and the roles table - an on/off toggle per role, a model, a temperature, an effort and fallbacks in order. An off role is grayed out and uses the primary.

**Not yet:** Ollama's `/api/show`, the scheduler with slots and privacy routing, the summary tier of compression (only the mechanical tier: old tool results trimmed, then whole earlier turns left out of the request), MCP, checkpoints and rewind, the trajectory files. Sub-agents and background tasks are built (2026-10-23, above).
