// Session: one WebSocket client (docs/10) - hello and resume, sub/unsub with replay or snapshot, requests (each on a
// thread of its own, answered as they finish), cancel, ping, ack and liveness. The connection's thread reads frames;
// a writer thread sends what the outbox holds, so a slow client never holds up anyone else. Every method but Run
// and FlushAndClose runs with the server lock held.

include "outbox.ch"
include "rpc.ch"
include "frames.ch"

namespace Ied
{
	struct DeviceInfo
	{
		public Core::String deviceId;
		public Core::String deviceClass;
		public Core::String deviceName;   // empty when the client gave none
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

		// Without the lock: waits (at most 2 s) for what is queued to go out, then closes the socket.
		public void FlushAndClose(int32 code, const char* reason);

		public Server* server;
		public Http::WebSocket* socket;
		public Core::String peer;
		public Core::String clientId;
		public DeviceInfo device;
		public Outbox outbox;
		public int64 idleTimeoutMs;
		public Core::HashMap<Core::String, bool> subs;        // false: receiving; true: cut off by a gap until re-subscribed
		public Core::HashMap<int64, CancelToken*> inflight;   // requests still running, by id
		public Core::HashMap<int64, CancelToken*> streams;    // streams requests opened, by request id
		public bool started;
		public bool closed;

	private:
		private bool AwaitHello(Serialization::DomNode& hello);
		private bool Start(const Serialization::DomNode& hello);
		private void ServeFrames();
		private FrameRead ReadFrame(Serialization::DomNode& frame, int32 timeoutMs);
		private int32 Dispatch(const Serialization::DomNode& frame);
		private void OnRequest(const Serialization::DomNode& frame);
		private void ServeRequest(Serialization::DomNode frame, CancelToken* token);
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

		private Threading::Thread writer;
		private int64 startedNs;
		private int32 closeCode;          // a frame asked to close the socket (a second hello)
	}

	// A frame's cursor for `key`, or -1 when it gives none.
	int64 CursorOf(const Serialization::DomNode& cursors, Core::StringView key);
}
