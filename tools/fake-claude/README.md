# fake-claude - a scriptable Claude Code CLI

The fixture for IED's Claude Code provider (`docs/04` "The Claude Code provider", `docs/19`): a stand-in for the owner's
`claude` CLI that speaks what IED uses of Claude Code 2.1.304's print mode, so the server checks
(`tools/server-checks/tests/test_claude.py`) run without a signed-in CLI and without spending usage. Stdlib only.

```bash
python -I tools/fake-claude/fake_claude.py --state <folder> --version          # 2.1.304 (Claude Code)
python -I tools/fake-claude/fake_claude.py --state <folder> auth status        # {"loggedIn": true, "authMethod": "claude.ai", ...}
python -I tools/fake-claude/fake_claude.py --state <folder> -p --input-format stream-json --output-format stream-json ...
```

IED runs it through `ai.claude.command`: `[python, "-I", "tools/fake-claude/fake_claude.py", "--state", <folder>]` - the
leading arguments the setting names come before IED's own.

## What it speaks

- **Control requests from IED:** `initialize` (with `sdkMcpServers: ["ied"]` it connects to IED's in-process MCP server
  first: `initialize`, `notifications/initialized`, `tools/list`), `get_usage` (answers `usage.json`), `set_model`,
  `interrupt`, `mcp_message` (a `notifications/tools/list_changed` makes it list the tools again); anything else is an
  error answer, as the real CLI's.
- **A user message** starts a query: `system/init` first (the session id, the tools as `mcp__ied__<name>`, `apiKeySource`),
  then the next scripted response as `stream_event`s (`message_start`, the content blocks, `message_delta` with the stop
  reason, `message_stop`). A response with tool calls asks IED for each with an `mcp_message` `tools/call` (its
  `_meta["claudecode/toolUseId"]` naming the block) and goes on with the next response once all are answered; the query ends
  with a `result` carrying `modelUsage.<model>.contextWindow` (1,000,000).
- **Errors:** a response with `error` is the synthetic assistant message the CLI writes (`error`, `is_api_error_message`) and an
  error result. An `interrupt` while a response streams ends the query with `error_during_execution`.
- **`--resume <id>`** of a session it never made prints "No conversation found with session ID: <id>" and exits 1.
- **API keys:** with `ANTHROPIC_API_KEY` in its environment it reports `authMethod: "api_key"` and `apiKeySource:
  "ANTHROPIC_API_KEY"`, as the real CLI would, so a check proves IED takes the key out (it never logs the value).

## The state folder

| File | Written by | Holds |
|---|---|---|
| `script.json` | the test | `{"responses": [...]}`, taken in order across queries; none left: "(the fake has no response queued)" |
| `usage.json` | the test | `get_usage`'s answer (default: not signed in to a plan) |
| `auth.json` | the test | `auth status`'s answer (default: signed in with `claude.ai`), plus `apiKeySource` for `system/init` |
| `log.jsonl` | the fake | each start (`argv`, `cwd`, the `CLAUDE_CODE_DISABLE_*` it got, `billed`: the names of billing variables it saw), every line it read (`in`), each tool list (`tools`), each query's content (`query`) and tool result (`toolResult`) |
| `sessions/` | the fake | one file per session id it made, for `--resume` |

A response is `{"blocks": [...], "stop"?, "delayMs"?, "rateLimit"?, "error"?, "text"?}`: blocks are `{"thinking": text}`,
`{"text": text}` (streamed a word per delta, `delayMs` apart) or `{"tool": name, "input": {...}, "id"?}`; `stop` defaults to
`tool_use` when there is a tool block, else `end_turn`; `rateLimit` is a `rate_limit_info` sent as a `rate_limit_event` first;
`error` (`rate_limit`, `authentication_failed`, ...) with `text` replaces the response with the CLI's error message.
