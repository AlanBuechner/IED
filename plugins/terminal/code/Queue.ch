// MessageQueue: messages for a writer thread, from any thread, in order; its own lock, never the server's.

include "bedrock.ch"

namespace Terminal
{
	class MessageQueue
	{
		[operator("create")] public void Init() {}

		public void Push(Core::StringView message);
		// The next message: true with `message`; false after `timeoutMs` with none, or once closed and empty.
		public bool Next(Core::String& message, int64 timeoutMs);
		// No more messages; Next drains what is left, then answers false.
		public void Close();
		public bool IsClosed();

		private Threading::Mutex m_Lock;
		private Threading::ConditionVariable m_Ready;
		private Core::Array<Core::String> m_Messages;
		private bool m_Closed;
	}
}
