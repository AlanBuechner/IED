// LanguageServer (docs/24 "Lifecycle"): one language server process of a workspace - started from a staged copy when
// its profile asks, initialized, written to by a thread of its own (a server that stops reading never holds the server
// lock), read by two more (its messages, its errors). Answers to requests wake the request waiting for them; what the
// server sends unasked goes to its pool. Members run with the server lock held; the threads take it to hand over.

include "profiles.ch"
include "queue.ch"
include "web/rpc.ch"

namespace Ied
{
	enum class LspState { Starting, Running, Failed, Stopped }

	const char* LspStateName(LspState state);
	ProfileVariables VariablesOf(const LspProfile* profile, const Workspace* ws);

	const int64 kLspStartTimeoutMs = 20000;
	const int64 kLspRequestTimeoutMs = 30000;
	const int64 kLspRestartWindowNs = 300000000000;   // three exits in five minutes and it stays down
	const int32 kLspMostRestarts = 3;
	const int32 kLspExitGraceMs = 2000;

	class LspAnswer
	{
		public bool done;
		public bool ok;
		public Core::String json;    // the result, or the error object
		public CancelToken* wake;    // the waiting request's token, notified when the answer comes; null when none waits
	}

	class LanguageServer
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server, Workspace* ws, LspProfile* profile);

		public Workspace* ws;
		public LspProfile* profile;
		public LspState state;
		public Core::String problem;
		public PositionEncoding encoding;
		public Core::String capabilities;                     // the server's, after initialize; "{}" before
		public uint32 pid;
		public int32 restarts;
		public Core::HashMap<Core::String, bool> documents;   // paths it has open (or opens once running)

		// Starts the process (again) and sends initialize; Failed with `problem` when it cannot start.
		public void Start();
		// Waits, the lock released, until the server runs, fails or stops, the request is cancelled, or `timeoutMs`.
		public bool AwaitRunning(Call& call, int64 timeoutMs);
		// A request and, the lock released, its answer: the result's JSON. Throws RpcError: cancelled, timeout,
		// unavailable (the server is not running or exited meanwhile), plugin_error {code, message} (the server's error).
		public Core::String Request(Call& call, Core::StringView method, Core::StringView params, int64 timeoutMs);
		public void Notify(Core::StringView method, Core::StringView params);
		// shutdown, exit, and the process ends; Stopped.
		public void Stop();
		// Stops it if it runs and starts it again, its exits forgotten (lsp.restart).
		public void Relaunch();

	private:
		private Core::String ProgramToRun(Core::String& error) const;
		private Process::StartInfo StartInfoFor(Core::StringView program) const;
		private Core::String InitializeParams() const;
		private int64 Ask(Core::StringView method, Core::StringView params, CancelToken* wake);
		private void Send(Core::StringView json);
		private void Dispatch(Core::StringView body);
		private void Answered(int64 id, const Serialization::DomNode& message);
		private void Initialized(const LspAnswer* answer);
		private void Exited();
		private void Fail(Core::StringView why);
		private void Restart(int64 run);
		private void ReadLoop(Process::Child* child, int64 run);
		private void ErrorLoop(Process::Child* child, int64 run);
		private void WriteLoop(Process::Child* child, MessageQueue* queue);

		private Server* server;
		private Process::Child* child;
		private MessageQueue* outgoing;
		private Core::HashMap<int64, LspAnswer*> answers;
		private int64 nextId;
		private int64 initializeId;
		private int64 run;                    // which start the threads belong to; a later start leaves earlier ones be
		private Core::Array<int64> exitTimes;
	}
}
