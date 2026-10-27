// DebugSession (docs/25 "Sessions"): one debug adapter process and the program it debugs. The session speaks DAP to
// the adapter - initialize, launch or attach, the workspace's breakpoints and exception filters, configurationDone -
// and keeps the state every client sees: building, starting, running, stopped, ended or failed, with a revision that
// grows each time the program stops or runs on. A writer thread of its own sends (an adapter that stops reading never
// holds the server lock); two more read its messages and its errors. Members run with the server lock held; what the
// adapter sends goes to the Debugger, which publishes it.

include "Providers.ch"
include "Usage.ch"
include "lsp/Queue.ch"
include "tasks/Model.ch"
include "services/Workspaces.ch"
include "web/Rpc.ch"

namespace Ied
{
	enum class DebugState { Building, Starting, Running, Stopped, Ended, Failed }

	const char* DebugStateName(DebugState state);

	const int64 c_DapRequestTimeoutMs = 30000;
	const int32 c_DapExitGraceMs = 2000;
	const int64 c_DapOutputLinesKept = 2000;

	const int32 c_DapConnectTimeoutMs = 10000;

	alias DapThen = func<void(bool ok, const Serialization::DomNode& body, Core::StringView message)>;

	// What carries DAP to the adapter: its standard streams, or the loopback socket it connected to (transport tcp).
	class DapChannel
	{
		public Process::Child* m_Child;
		public Net::TcpStream* m_Stream;

		public int64 Read(void* buffer, int64 capacity) { return m_Stream ? m_Stream->Receive(buffer, capacity) : m_Child->Read(Process::Stream::Output, buffer, capacity); }
		public bool Write(Core::StringView text) { return m_Stream ? m_Stream->SendText(text) : m_Child->WriteText(text); }
	}

	class DapAnswer
	{
		public bool m_Done;
		public bool m_Ok;
		public Core::String m_Body;       // the response's body as JSON ("null" without one)
		public Core::String m_Message;    // a failed response's message
		public CancelToken* m_Wake;       // a waiting request's token
		public DapThen m_Then;            // or what to do with it
		public bool m_HasThen;
	}

	class DebugSession
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server, Workspace* ws, Core::StringView id, const DebugLaunch& launch);

		public Core::String m_Id;
		public Workspace* m_Ws;
		public DebugLaunch m_Launch;
		public DapProvider* m_Provider;
		public DebugState m_State;
		public int64 m_Revision;
		public Core::String m_Problem;
		public Core::String m_BuildRunId;      // the pre-launch task's run while it builds
		public int64 m_ExitCode;
		public bool m_Exited;
		public Core::String m_Capabilities;    // the adapter's, after initialize
		public int64 m_StopThread;
		public Core::String m_StopReason;
		public Core::String m_StopDescription;
		public Core::String m_Threads;         // the last threads answer's list, as JSON
		public Core::String m_Frames;          // the stopped thread's top frames as the adapter gave them, as JSON
		public Core::Array<Core::String> m_Output;
		public bool m_Configured;
		public ProgramUsage* m_Usage;          // once the adapter names the program's process (Diagnostic Tools)

		// Starts the adapter and sends initialize; Failed with `problem` when it cannot start.
		public void Start(DapProvider* provider);
		// A request and, the lock released, its body's JSON. Throws RpcError: unavailable (no adapter), timeout,
		// cancelled, plugin_error (the adapter refused it, with its message).
		public Core::String Request(Call& call, Core::StringView command, Core::StringView arguments);
		// A request whose response goes to `then` (lock held), from the reader thread.
		public void Ask(Core::StringView command, Core::StringView arguments, DapThen then);
		// Ends the program (terminate, else disconnect) and then the adapter.
		public void Stop();
		public void Fail(Core::StringView why);
		// The program runs on (a continue or a step was answered): the revision grows and the stop is gone.
		public void Ran();
		public bool IsLive() const { return m_State != DebugState::Ended && m_State != DebugState::Failed; }
		// {id, name, project, type, state, revision, problem, exitCode, stop: {threadId, reason, description} | null}
		public Core::String SummaryJson() const;

	private:
		private Core::String ProgramToRun(Core::String& error) const;
		private Process::StartInfo StartInfoFor(Core::StringView program, int32 port) const;
		private int64 Send(Core::StringView command, Core::StringView arguments, DapAnswer* answer);
		private void Write(Core::StringView json);
		private void Initialized(bool ok, const Serialization::DomNode& body, Core::StringView message);
		private void Dispatch(Core::StringView body);
		private void Answered(const Serialization::DomNode& message);
		private void Respond(const Serialization::DomNode& request);
		private void Happened(Core::StringView event, const Serialization::DomNode& body);
		private void Stopped(const Serialization::DomNode& body);
		private void Named(const Serialization::DomNode& body);
		private void FetchFrames(int64 stopped);
		private void Continued();
		private void Terminated();
		private void Exited();
		private void Talk(DapChannel* channel, Process::Child* process, MessageQueue* queue, int64 run);
		private void AcceptLoop(Net::TcpListener* listener, Process::Child* process, MessageQueue* queue, int64 run);
		private void ReadLoop(DapChannel* channel, Process::Child* process, int64 run);
		private void ErrorLoop(Process::Child* process, Process::Stream stream, int64 run);
		private void WriteLoop(DapChannel* channel, MessageQueue* queue);

		private Server* m_Server;
		private Process::Child* m_Child;
		private MessageQueue* m_Outgoing;
		private Core::HashMap<int64, DapAnswer*> m_Answers;
		private int64 m_NextSeq;
		private int64 m_Run;
	}
}
