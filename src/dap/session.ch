// DebugSession (docs/25 "Sessions"): one debug adapter process and the program it debugs. The session speaks DAP to
// the adapter - initialize, launch or attach, the workspace's breakpoints and exception filters, configurationDone -
// and keeps the state every client sees: building, starting, running, stopped, ended or failed, with a revision that
// grows each time the program stops or runs on. A writer thread of its own sends (an adapter that stops reading never
// holds the server lock); two more read its messages and its errors. Members run with the server lock held; what the
// adapter sends goes to the Debugger, which publishes it.

include "providers.ch"
include "usage.ch"
include "lsp/queue.ch"
include "tasks/model.ch"
include "services/workspaces.ch"
include "web/rpc.ch"

namespace Ied
{
	enum class DebugState { Building, Starting, Running, Stopped, Ended, Failed }

	const char* DebugStateName(DebugState state);

	const int64 kDapRequestTimeoutMs = 30000;
	const int32 kDapExitGraceMs = 2000;
	const int64 kDapOutputLinesKept = 2000;

	const int32 kDapConnectTimeoutMs = 10000;

	alias DapThen = func<void(bool ok, const Serialization::DomNode& body, Core::StringView message)>;

	// What carries DAP to the adapter: its standard streams, or the loopback socket it connected to (transport tcp).
	class DapChannel
	{
		public Process::Child* child;
		public Net::TcpStream* stream;

		public int64 Read(void* buffer, int64 capacity) { return stream ? stream->Receive(buffer, capacity) : child->Read(Process::Stream::Output, buffer, capacity); }
		public bool Write(Core::StringView text) { return stream ? stream->SendText(text) : child->WriteText(text); }
	}

	class DapAnswer
	{
		public bool done;
		public bool ok;
		public Core::String body;       // the response's body as JSON ("null" without one)
		public Core::String message;    // a failed response's message
		public CancelToken* wake;       // a waiting request's token
		public DapThen then;            // or what to do with it
		public bool hasThen;
	}

	class DebugSession
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server, Workspace* ws, Core::StringView id, const DebugLaunch& launch);

		public Core::String id;
		public Workspace* ws;
		public DebugLaunch launch;
		public DapProvider* provider;
		public DebugState state;
		public int64 revision;
		public Core::String problem;
		public Core::String buildRunId;      // the pre-launch task's run while it builds
		public int64 exitCode;
		public bool exited;
		public Core::String capabilities;    // the adapter's, after initialize
		public int64 stopThread;
		public Core::String stopReason;
		public Core::String stopDescription;
		public Core::String threads;         // the last threads answer's list, as JSON
		public Core::String frames;          // the stopped thread's top frames as the adapter gave them, as JSON
		public Core::Array<Core::String> output;
		public bool configured;
		public ProgramUsage* usage;          // once the adapter names the program's process (Diagnostic Tools)

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
		public bool IsLive() const { return state != DebugState::Ended && state != DebugState::Failed; }
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

		private Server* server;
		private Process::Child* child;
		private MessageQueue* outgoing;
		private Core::HashMap<int64, DapAnswer*> answers;
		private int64 nextSeq;
		private int64 run;
	}
}
