// LanguageServer (docs/24 "Lifecycle"): one language server process of a workspace - started from a staged copy when
// its profile asks, initialized, written to by a thread of its own (a server that stops reading never holds the server
// lock), read by two more (its messages, its errors). Answers to requests wake the request waiting for them; what the
// server sends unasked goes to its pool. Members run with the server lock held; the threads take it to hand over.

include "Profiles.ch"
include "Queue.ch"
include "web/Rpc.ch"

namespace Ied
{
	enum class LspState { Starting, Running, Failed, Stopped }

	const char* LspStateName(LspState state);

	// The LSP's own error codes for an answer that is no answer: the request was cancelled, or the document changed
	// under it (the client asks again for the new text).
	const int64 c_LspRequestCancelled = -32800;
	const int64 c_LspContentModified = -32801;

	// A server's error object as the protocol's error: `cancelled` for the two above, else plugin_error {code, message}.
	RpcError LspServerError(Core::StringView json);

	const int64 c_LspStartTimeoutMs = 20000;
	const int64 c_LspRequestTimeoutMs = 30000;
	const int64 c_LspRestartWindowNs = 300000000000;   // three exits in five minutes and it stays down
	const int32 c_LspMostRestarts = 3;
	const int32 c_LspExitGraceMs = 2000;

	class LspAnswer
	{
		public bool m_Done;
		public bool m_Ok;
		public Core::String m_Json;    // the result, or the error object
		public CancelToken* m_Wake;    // the waiting request's token, notified when the answer comes; null when none waits
	}

	// A file plugin code asked about that no document holds, opened on the server alone (docs/24 "Plugin code asks
	// language servers").
	class TransientFile
	{
		public Core::String m_Path;     // the workspace path, or the file: URI of a file outside it
		public int64 m_Revision;        // its revision on disk when its text was sent
		public int64 m_Version;
	}

	class LanguageServer
	{
		[operator("create")] public void Init() {}
		// `project` empty: the workspace-wide server of `profile`.
		[operator("create")] public void Init(Server* server, Workspace* ws, LspProfile* profile, Core::StringView project, const FS::Path& projectDir);

		public Workspace* m_Ws;
		public LspProfile* m_Profile;
		public Core::String m_Project;
		public FS::Path m_ProjectDir;
		public LspState m_State;
		public Core::String m_Problem;
		public PositionEncoding m_Encoding;
		public Core::String m_Capabilities;                     // the server's, after initialize; "{}" before
		public uint32 m_Pid;
		public int32 m_Restarts;
		public Core::HashMap<Core::String, bool> m_Documents;   // paths it has open
		public Core::HashMap<Core::String, bool> m_Published;   // paths it published diagnostics for
		public Core::Array<TransientFile*> m_Transient;         // of `documents`, those open for plugin code alone, oldest first
		public Core::HashMap<Core::String, bool> m_Progress;    // $/progress tokens begun and not ended: work it does (indexing)
		public bool m_Progressed;                               // it began some since it last started
		public int64 m_StartedNs;                               // when it last started
		public int64 m_UsedNs;                                  // when it was last asked or told something, or a window last showed one of its documents
		public Core::String m_OptionsFilesSent;                 // its initializationOptionsFiles as it last got them (JSON)

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
		// Whether a request to it waits for its answer.
		public bool Answering() const { return m_Answers.Count() > 0; }
		// What its profile's ${...} variables stand for.
		public ProfileVariables Variables() const;
		// The source of its diagnostics: the profile id, "/<project>" added for a project's server.
		public Core::String Source() const;

	private:
		private Core::String ProgramToRun(Core::String& error) const;
		private Process::StartInfo StartInfoFor(Core::StringView program) const;
		private Core::String InitializeParams();
		private int64 Ask(Core::StringView method, Core::StringView params, CancelToken* wake);
		private void Send(Core::StringView json);
		private void Dispatch(Core::StringView body);
		private void Answered(int64 id, const Serialization::DomNode& message);
		private void Initialized(const LspAnswer* answer);
		private void Exited();
		// Every request waiting for an answer gets `why` as the server's error (it stopped or exited).
		private void FailAnswers(Core::StringView why);
		private void Fail(Core::StringView why);
		private void Restart(int64 run);
		private void ReadLoop(Process::Child* child, int64 run);
		private void ErrorLoop(Process::Child* child, int64 run);
		private void WriteLoop(Process::Child* child, MessageQueue* queue);

		private Server* m_Server;
		private Process::Child* m_Child;
		private MessageQueue* m_Outgoing;
		private Core::HashMap<int64, LspAnswer*> m_Answers;
		private int64 m_NextId;
		private int64 m_InitializeId;
		private int64 m_Run;                    // which start the threads belong to; a later start leaves earlier ones be
		private Core::Array<int64> m_ExitTimes;
	}
}
