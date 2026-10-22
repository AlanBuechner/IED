// The owner's Claude Code CLI as a child process (docs/04 "The Claude Code provider"): where it is, and a running one spoken
// to in stream-json, a JSON object a line each way. A reader thread splits what it prints into lines (its standard error
// with its output; a line that is not JSON is kept as a note); whoever drives the process takes them in order with Next,
// and a control request IED sends waits for its answer while the lines before it stay for Next.

include "bedrock.ch"

namespace Ai
{
	const int64 kClaudeNotesKept = 20;
	const int32 kClaudeRequestMs = 30000;
	const int32 kClaudeCommandMs = 15000;   // `claude --version`, `claude auth status`

	// The CLI's program and leading arguments: `configured` (the ai.claude.command setting) when it names one, else
	// claude.exe on PATH, the native installer's (%USERPROFILE%\.local\bin), then the newest the Claude desktop app keeps
	// (%APPDATA%\Claude\claude-code\<version>\<build>\claude.exe). Empty when there is none.
	Core::Array<Core::String> FindClaude(const Core::Array<Core::String>& configured);
	// The settings value of ai.claude.command (a JSON array of strings) as a list; empty when it names nothing.
	Core::Array<Core::String> ClaudeCommandSetting(Core::StringView json);
	// Whether `a` is a later version than `b` ("2.1.304" > "2.1.299").
	bool LaterVersion(Core::StringView a, Core::StringView b);
	// The changes to the environment IED's CLI processes run with: every way to bill an API key or another provider
	// instead of the owner's subscription taken out (ANTHROPIC_API_KEY, ANTHROPIC_AUTH_TOKEN, ANTHROPIC_BASE_URL,
	// CLAUDE_CODE_USE_BEDROCK / _VERTEX / _FOUNDRY), what a Claude Code session the server was started from hands on
	// (CLAUDE_CODE_SIMPLE, CLAUDECODE, its session's ids and sockets) taken out too, and the CLI's own CLAUDE.md and AGENTS.md
	// reading, memory, bundled skills, background tasks, cron, workflows, the advisor tool and file checkpoints off (docs/04).
	Core::Array<Core::String> ClaudeEnvironment();
	// How `claude auth status` says the CLI signs in - its authMethod: claude.ai, oauth_token, api_key, api_key_helper,
	// third_party, none - and whether it is signed in; empty when it did not answer. It never reads a credential.
	Core::String ClaudeAuthMethod(const Core::Array<Core::String>& command, Core::StringView folder, bool& signedIn);
	// Whether `method` runs on the owner's Claude subscription: a claude.ai sign-in, or its long-lived token
	// (`claude setup-token`). An API key, a key helper or another provider never does (docs/04 "Subscription only").
	bool IsSubscriptionAuth(Core::StringView method);
	// The first word of `claude --version` ("2.1.304"); empty when it did not answer.
	Core::String ClaudeVersion(const Core::Array<Core::String>& command, Core::StringView folder);
	// Why a session does not start when the CLI signs in with `method` (not the subscription).
	Core::String NotSubscription(Core::StringView method);
	// The arguments after the program every IED CLI process takes: print mode spoken in stream-json, no built-in tools,
	// no MCP servers but IED's, no settings files or slash commands.
	Core::Array<Core::String> ClaudeBaseArguments();

	class ClaudeLine
	{
		public Serialization::DomDoc* doc;
		public Serialization::DomNode root;   // invalid for a line that is not JSON
		public Core::String text;

		public Core::StringView Type() const { return root.Member("type").AsView(); }
	}

	alias ClaudeServe = func<void(ClaudeLine* line)>;

	class ClaudeProcess
	{
		[operator("create")] public void Init() {}

		// Starts `command` (the program, then its arguments) in `folder`, with `environment` ("NAME=value") added to the
		// server's; false with `error`.
		public bool Start(const Core::Array<Core::String>& command, Core::StringView folder, const Core::Array<Core::String>& environment, Core::String& error);
		public bool Running() const;
		// One line to the CLI (a JSON object; the line break is added).
		public bool Send(Core::StringView line);
		// The next line, waiting at most `waitMs`: null on a timeout, and once the process has ended and every line was
		// taken (Ended() tells the two apart).
		public ClaudeLine* Next(int32 waitMs);
		// `kept` (taken with Next, in order) back before the lines not taken yet.
		public void PutBack(const Core::Array<ClaudeLine*>& kept);
		public bool Ended();
		// Sends the control request `request` (the request object's JSON) and waits at most `waitMs` for its answer: the
		// answer's `response` node (an empty object's when it has none), or an invalid node with `error`. A control
		// request of the CLI's that comes meanwhile (an MCP message) goes to `serve`; other lines stay for Next.
		public Serialization::DomNode Request(Core::StringView request, int32 waitMs, Core::String& error, ClaudeServe serve);
		// The last lines it printed that are not JSON (kClaudeNotesKept), one per line.
		public Core::String Notes();
		// Ends the CLI and what it started.
		public void Stop();

		private void ReadLoop(Process::Child* process);
		private void Take(Core::StringView text);

		private Process::Child* child;
		private Threading::Mutex writing;         // one line at a time to its input
		private Threading::Mutex mutex;
		private Threading::ConditionVariable arrived;
		private Core::Array<ClaudeLine*> lines;   // read, not taken yet
		private Core::Array<Core::String> notes;
		private bool ended;
		private int64 requests;
	}
}
