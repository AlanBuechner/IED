// A chat's Claude Code session (docs/04 "A chat's session"): the CLI process that holds the chat's conversation with Claude,
// kept between turns; the session id it resumes from, the chat's turns it has seen, the model and effort it runs and the
// system message it was started with. It answers the CLI's own control requests - IED's in-process MCP server (initialize,
// tools/list) and its permission prompts (only IED's tools are allowed) - and keeps each tools/call until the run that
// owns its tool_use answers it.

include "claudecli.ch"
include "claudewire.ch"

namespace Ai
{
	const int64 kClaudeIdleMs = 10 * 60 * 1000;   // a session no turn used for this long stops (it resumes in seconds)

	class ClaudeSession
	{
		public Core::String chatId;
		public ClaudeProcess* process;               // null before it starts and after it stops
		public Core::String sessionId;               // the CLI's, for --resume
		public Core::Array<Core::String> turns;      // the chat's turns the session has
		public Core::String model;                   // what it runs now
		public Core::String effort;
		public uint64 systemHash;                    // the system message it started with
		public Core::String tools;                   // tools/list's result
		public bool querying;                        // a message went in and the CLI owes its result
		public bool inUse;                           // a turn drives it (host lock)
		public int64 idleSinceMs;                    // host lock
		public Core::Array<ClaudeLine*> calls;       // tools/call requests not answered yet

		public bool Running() const { return process != null && process->Running(); }
		// Starts `command` with the session's arguments in `folder`: the system message from `systemFile`, `model`,
		// `effort` when set, and --resume when there is a session id. Then initializes it, naming IED's MCP server; false
		// with `error`.
		public bool Start(const Core::Array<Core::String>& command, Core::StringView folder, Core::StringView systemFile, Core::String& error);
		// A control request of the CLI's: answered now, or a tools/call kept in `calls`.
		public void Serve(ClaudeLine* line);
		// The tools/call for `toolUseId`, waiting at most `waitMs` while other control requests are served; null when the
		// CLI settled that call itself or asked for none.
		public ClaudeLine* AwaitCall(Core::StringView toolUseId, int32 waitMs);
		// Answers a tools/call with an MCP result (McpCallResult).
		public void Answer(ClaudeLine* call, Core::StringView result);
		// The model it runs from now on; false with `error`.
		public bool SetModel(Core::StringView id, Core::String& error);
		// The tools/list result from now on; the CLI is told the list changed.
		public void SetTools(Core::StringView result);
		// Ends the query in flight - interrupted, or read to its result - within `waitMs` (else the CLI is stopped); calls
		// still waiting are answered `note`, and the other lines go to `other` (rate limit events, the result).
		public void Settle(Core::StringView note, bool interrupt, int32 waitMs, ClaudeServe other);
		public void Stop();
	}

	// The tool_use a tools/call request runs (its _meta's claudecode/toolUseId).
	Core::StringView ToolUseOf(const ClaudeLine* call);
}
