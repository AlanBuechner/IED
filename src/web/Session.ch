// Session: one WebSocket client (docs/10) - hello and resume, sub/unsub with replay or snapshot, requests (each on a
// thread of its own, answered as they finish), cancel, ping, ack and liveness. The connection's thread reads frames;
// a writer thread sends what the outbox holds, so a slow client never holds up anyone else. Every method but Run
// and FlushAndClose runs with the server lock held.

include "Outbox.ch"
include "Rpc.ch"
include "Frames.ch"

namespace Ied
{
	struct DeviceInfo
	{
		public Core::String m_DeviceId;
		public Core::String m_DeviceClass;
		public Core::String m_DeviceName;   // empty when the client gave none
	}

	// How reading a frame ended.
	enum class FrameRead { Frame, Idle, Closed }

	class Session
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server, Http::WebSocket* socket, Core::StringView peer);

		// On the connection's thread: the whole life of the session.
		public void Run();

		// Called by the hub for an event of a subscribed topic.
		public void Deliver(Topic* topic, const Event* event);
		// A topic that went away: no longer subscribed.
		public void Forget(Core::StringView topic);
		public void Push(Core::StringView frame);
		public void PushError(int64 id, const RpcError& error);
		// The client's identity as the server topic shows it.
		public Core::String ClientJson() const;

		// Without the lock, from any thread: waits (at most 2 s) for what is queued to go out, then closes the socket; once
		// the connection's thread has ended (its socket gone with it), nothing.
		public void FlushAndClose(int32 code, const char* reason);

		public Server* m_Server;
		public Http::WebSocket* m_Socket;
		public Core::String m_Peer;
		public Core::String m_AuthDevice;   // the device session the socket was opened with (docs/11)
		public bool m_Local;                // the socket came from a loopback address: a client on the server's machine
		public Core::String m_ClientId;
		public DeviceInfo m_Device;
		public Outbox m_Outbox;
		public int64 m_IdleTimeoutMs;
		public Core::HashMap<Core::String, bool> m_Subs;        // false: receiving; true: cut off by a gap until re-subscribed
		public Core::HashMap<int64, CancelToken*> m_Inflight;   // requests still running, by id
		public Core::HashMap<int64, CancelToken*> m_Streams;    // streams requests opened, by request id
		public bool m_Started;
		public bool m_Closed;

	private:
		private bool AwaitHello(Serialization::DomNode& hello);
		private bool Start(const Serialization::DomNode& hello);
		private void ServeFrames();
		private FrameRead ReadFrame(Serialization::DomNode& frame, int32 timeoutMs);
		private int32 Dispatch(const Serialization::DomNode& frame);
		private void OnRequest(const Serialization::DomNode& frame);
		private void ServeRequest(Serialization::DomNode frame, CancelToken* token, int64 ticket);
		private void OnCancel(int64 id);
		private void OnSubscribe(const Serialization::DomNode& frame);
		private void OnUnsubscribe(const Serialization::DomNode& topics);
		private Core::String ResumeStatus(const Topic* topic, int64 cursor);
		private void Attach(Topic* topic, Core::StringView status, int64 cursor);
		private void PushEvent(Topic* topic, const Event* event);
		private void Welcome(const Serialization::DomNode& hello);
		private void Cleanup();
		private void WriteLoop();
		private bool Idle(int64 nowNs) const;
		private void Close(int32 code, const char* reason);

		private Threading::Thread m_Writer;
		private int64 m_StartedNs;
		private int32 m_CloseCode;          // a frame asked to close the socket (a second hello)
		// Held while a thread other than the connection's uses the socket: the socket lives on the connection thread's
		// stack, and that thread takes this before it returns.
		private Threading::Mutex m_SocketUse;
		private bool m_SocketGone;
		// Requests sent and started (under the lock): each starts once those sent before it have.
		private int64 m_RequestsSent;
		private int64 m_RequestsStarted;
		private Threading::ConditionVariable m_RequestTurn;
	}

	// A frame's cursor for `key`, or -1 when it gives none.
	int64 CursorOf(const Serialization::DomNode& cursors, Core::StringView key);
}
