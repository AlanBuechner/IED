// The Chrome DevTools Protocol over one WebSocket to the browser (docs/27 "Engine"): commands with ids, their replies
// matched by id, and events handed to one handler - each page's on its flattened session (a "sessionId" field). One
// reader thread receives; calls block their own thread until the reply comes or the time is up. A handler never calls
// back into the connection (the reader would wait for itself): work that needs a reply goes to another thread.

include "bedrock.ch"

namespace Browser
{
	const int32 c_CdpCallMs = 15000;
	const int64 c_CdpMessageBytes = 256 * 1024 * 1024;

	// A reply: `result` (a node of `doc`) when ok, else the browser's message or why none came.
	class CdpReply
	{
		public bool m_Ok;
		public Core::String m_Error;
		public Serialization::DomDoc* m_Doc;
		public Serialization::DomNode m_Result;
	}

	// An event: its method, its session ("" for the browser's own) and its params (a node of `doc`).
	class CdpEvent
	{
		public Core::String m_Method;
		public Core::String m_SessionId;
		public Serialization::DomDoc* m_Doc;
		public Serialization::DomNode m_Params;
	}

	alias CdpEventHandler = func<void(const CdpEvent& event)>;
	alias CdpClosedHandler = func<void(Core::StringView why)>;

	class CdpWaiter
	{
		public bool m_Done;
		public CdpReply m_Reply;
	}

	class CdpConnection
	{
		// Connects to the browser endpoint ("ws://127.0.0.1:<port>/devtools/browser/<id>"); false with `error`.
		public bool Connect(Core::StringView url, Core::String& error);
		// Receives until the connection closes; run it on a thread of its own once events are wanted.
		public void ReadLoop(CdpEventHandler onEvent, CdpClosedHandler onClosed);
		// A command: `paramsJson` an object's JSON ("" for none), `sessionId` "" for the browser itself.
		public CdpReply Call(Core::StringView sessionId, Core::StringView method, Core::StringView paramsJson, int32 timeoutMs = c_CdpCallMs);
		// A command whose reply nobody waits for (acknowledgements).
		public void Send(Core::StringView sessionId, Core::StringView method, Core::StringView paramsJson);
		public bool IsOpen() const { return m_Open.Load(); }
		public void Close();

	private:
		private int64 Post(Core::StringView sessionId, Core::StringView method, Core::StringView paramsJson, CdpWaiter* waiter);
		private void Answer(int64 id, const CdpReply& reply);
		private void FailAll(Core::StringView why);

		private Http::WebSocket m_Socket;
		private Threading::Atomic<bool> m_Open;
		private Threading::Mutex m_Mutex;              // ids and waiters
		private Threading::ConditionVariable m_Answered;
		private int64 m_NextId;
		private Core::HashMap<int64, CdpWaiter*> m_Waiting;
	}

	// The JSON text of a string, quoted and escaped.
	Core::String JsonText(Core::StringView text);
}
