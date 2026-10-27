// Outbox: what one client has yet to receive, sent by the client's own writer thread so that no producer ever waits
// for a slow client (docs/10 backpressure). Once more than `coalesceBytes` wait, an event of a coalescible topic merges
// into the last one queued when they match; past `budget`, the queued events of the heaviest topic are dropped and the
// client gets a gap instead. Thread-safe: producers hold the server lock, the writer holds none.

include "Hub.ch"

namespace Ied
{
	const int64 c_MaxMergedBytes = 1048576;

	class OutItem
	{
		public Core::String m_Text;
		public int64 m_Bytes;
		public Core::String m_Topic;   // empty for a frame of no topic (res, snap, gap, pong...)
		public int64 m_FirstSeq;
		public int64 m_Seq;
		public Core::String m_Kind;
		public bool m_Mergeable;
		public Merge m_Merge;
	}

	// A topic whose queued events [first, last] the budget dropped.
	struct Gap
	{
		public bool m_Dropped;
		public Core::String m_Topic;
		public int64 m_First;
		public int64 m_Last;
	}

	class Outbox
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 budget, int64 coalesceBytes) { this.m_Budget = budget; this.m_CoalesceBytes = coalesceBytes; }

		public void PushControl(Core::StringView text);
		public Gap PushEvent(Core::StringView topic, const Event* event);
		public void DropTopic(Core::StringView topic);
		// The writer's next frame: false once closed, or when none comes within `timeoutMs`.
		public bool Next(Core::String& text, int32 timeoutMs);
		// Wakes the writer for good; queued frames are dropped.
		public void Close();
		public bool IsEmpty();
		public bool IsClosed();
		public int64 Bytes();
		public int64 Budget();
		public int64 CoalesceBytes();
		public void Configure(int64 budget, int64 coalesceBytes);

	private:
		private void Append(OutItem* item);
		private bool MergeIntoTail(Core::StringView topic, const Event* event);
		private Gap EnforceBudget();
		private void Remove(Core::StringView topic);
		private Core::StringView Heaviest() const;

		private Threading::Mutex m_Mutex;
		private Threading::ConditionVariable m_Ready;
		private Core::Queue<OutItem*> m_Items;
		private Core::HashMap<Core::String, int64> m_TopicBytes;
		private int64 m_Bytes;
		private int64 m_Budget;
		private int64 m_CoalesceBytes;
		private bool m_Closed;
	}
}
