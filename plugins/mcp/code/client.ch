// A connection to one MCP server (docs/04 "MCP" Protocol). stdio: the program IED starts, spoken to one JSON-RPC message a
// line each way, a reader thread taking what it prints - its output as messages, its standard error as log lines.
// Streamable HTTP: a POST for each message, whose answer is JSON or an event stream that ends with it, the session the
// server gives at initialize carried by every later request. Call waits for its answer at most the time given or until
// the flag `cancelled` points at is set, and a call given up is cancelled at the server (notifications/cancelled). The
// server's requests are answered here (ping; anything else kMethodNotFound); its notifications, its log lines and its end
// go to the handlers the opener gave, on the reader thread (http: on the calling thread).

include "jsonrpc.ch"
include "http.ch"

namespace Mcp
{
	const char* kClientVersion = "0.1.0";
	const int32 kWriteMs = 10000;
	const int32 kSliceMs = 100;
	const int32 kConnectMs = 10000;
	const int64 kErrorBodyBytes = 400;

	alias LogHandler = func<void(Core::StringView line)>;
	alias NotifyHandler = func<void(Message* message)>;
	alias EndHandler = func<void(int64 exitCode)>;

	// What a connection starts or reaches: a server's configuration with its environment read.
	class ServerLaunch
	{
		public Core::String program;                    // stdio: a path, or a name found on PATH
		public Core::Array<Core::String> arguments;
		public Core::String shellLine;                  // stdio through cmd.exe (a batch file such as npx.cmd): the line
		public Core::String cwd;
		public Core::Array<Core::String> environment;   // "NAME=value", added to IED's
		public Core::String url;                        // http
		public Core::Array<Core::String> headers;       // "Name: value"
	}

	class PendingCall
	{
		public int64 id;
		public bool done;
		public Message* answer;
	}

	class McpClient
	{
		[operator("create")] public void Init() {}

		// stdio: starts the program; http: checks the URL. False with `error`.
		public bool Open(const ServerLaunch& launch, LogHandler logged, NotifyHandler notified, EndHandler ended, Core::String& error);
		// initialize, then notifications/initialized: the server's name, version, protocol version, instructions and
		// whether it says when its tools change are kept.
		public bool Initialize(int32 waitMs, const bool* cancelled, Core::String& error);
		// The `method` request's result (an empty object's when it gave none), or an invalid node with `error`: its error
		// object described, no answer in time, stopped, or the server gone.
		public Serialization::DomNode Call(Core::StringView method, Core::StringView paramsJson, int32 waitMs, const bool* cancelled, Core::String& error);
		public bool Notify(Core::StringView method, Core::StringView paramsJson);
		// stdio: the process has not ended; http: the session was not closed.
		public bool Alive();
		// The process's exit code once it ended; -1 before, and for http.
		public int64 ExitCode();
		public uint32 Pid() const;
		public bool IsHttp() const { return http; }
		// stdio: its input closed, and the process and what it started ended after a moment; http: the session's DELETE.
		public void Close();

		public Core::String serverName;
		public Core::String serverVersion;
		public Core::String protocolVersion;
		public Core::String instructions;
		public bool toolsChange;                 // capabilities.tools.listChanged

		private Serialization::DomNode CallStdio(int64 id, Core::StringView text, int32 waitMs, const bool* cancelled, Core::String& error);
		private Serialization::DomNode CallHttp(int64 id, Core::StringView text, int32 waitMs, const bool* cancelled, bool initializing, Core::String& error);
		private Serialization::DomNode Settled(PendingCall* call, Core::String& error);
		private void GiveUp(int64 id, Core::StringView why);
		private bool WriteLine(Core::StringView text);
		private bool Post(Core::StringView text, Core::String& error);
		private Core::Array<Core::String> HttpHeaders();
		private void ReadLoop(Process::Child* process);
		private void Dispatch(Core::StringView text, int64 waiting, Message*& answer);
		private void Answer(Message* request);
		private int64 NextId();

		private ServerLaunch launch;
		private LogHandler logged;
		private NotifyHandler notified;
		private EndHandler onEnd;
		private bool http;
		private Process::Child* child;
		private Threading::Mutex writing;
		private Threading::Mutex mutex;
		private Threading::ConditionVariable arrived;
		private Core::Array<PendingCall*> pending;
		private bool ended;
		private bool closed;
		private int64 exitCode;
		private int64 ids;
		private Core::String session;            // Mcp-Session-Id
	}

	// The line cmd.exe runs for `argv` (a batch file and its arguments), each word quoted; false with `problem` when a
	// word holds what cmd.exe reads even inside quotes (a quote, '%', a line break).
	bool BatchLine(const Core::Array<Core::String>& argv, Core::String& line, Core::String& problem);
	// `command` as a path: itself when it names a folder or has an extension and exists, else the first PATH folder's
	// <command>.exe, .cmd, .bat or .com; empty when none is found.
	Core::String FindProgram(Core::StringView command, Core::StringView path);
	bool IsBatchFile(Core::StringView program);
}
