// A chat's Claude Code session (docs/04 "A chat's session"): the CLI process that holds the chat's conversation with Claude,
// kept between turns; the session id it resumes from, the chat's turns it has seen, the model and effort it runs and the
// system message it was started with. It answers the CLI's own control requests - IED's in-process MCP server (initialize,
// tools/list) and its permission prompts (only IED's tools are allowed) - and keeps each tools/call until the run that
// owns its tool_use answers it.
//
// Its members read m_Process once per call: Stop() from another thread (the plugin stopping) ends that process and
// clears the field, and a member in the middle of a call then sees an ended process, never a null one.

include "ClaudeCli.ch"
include "ClaudeWire.ch"

namespace Ai
{
	const int64 c_ClaudeIdleMs = 10 * 60 * 1000;   // a session no turn used for this long stops (it resumes in seconds)

	class ClaudeSession
	{
		public Core::String m_ChatId;
		public ClaudeProcess* m_Process;               // null before it starts and after it stops
		public Core::String m_SessionId;               // the CLI's, for --resume
		public Core::Array<Core::String> m_Turns;      // the chat's turns the session has
		public Core::String m_Model;                   // what it runs now
		public Core::String m_Effort;
		public uint64 m_SystemHash;                    // the system message it started with
		public Core::String m_Tools;                   // tools/list's result
		public bool m_Querying;                        // a message went in and the CLI owes its result
		// Who drives it now - a turn ("turn:<turnId>") or the chat's context count ("count") - one at a time; empty when
		// free, and only then may the idle keeper stop it (host lock). Each user frees only its own hold.
		public Core::String m_User;
		public int64 m_IdleSinceMs;                    // host lock
		public Core::Array<ClaudeLine*> m_Calls;       // tools/call requests not answered yet

		public bool Running() const { return m_Process != null && m_Process->Running(); }
		public bool InUse() const { return !m_User.IsEmpty(); }
		// Frees the session when `user` holds it (host lock); false when another does.
		public bool Release(Core::StringView user, int64 nowMs);
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
