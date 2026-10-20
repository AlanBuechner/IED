// Outbox: what one client has yet to receive, sent by the client's own writer thread so that no producer ever waits
// for a slow client (docs/10 backpressure). Once more than `coalesceBytes` wait, an event of a coalescible topic merges
// into the last one queued when they match; past `budget`, the queued events of the heaviest topic are dropped and the
// client gets a gap instead. Thread-safe: producers hold the server lock, the writer holds none.

include "hub.ch"

namespace Ied
{
	const int64 kMaxMergedBytes = 1048576;

	class OutItem
	{
		public Core::String text;
		public int64 bytes;
		public Core::String topic;   // empty for a frame of no topic (res, snap, gap, pong...)
		public int64 firstSeq;
		public int64 seq;
		public Core::String kind;
		public bool mergeable;
		public Merge merge;
	}

	// A topic whose queued events [first, last] the budget dropped.
	struct Gap
	{
		public bool dropped;
		public Core::String topic;
		public int64 first;
		public int64 last;
	}

	class Outbox
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 budget, int64 coalesceBytes) { this.budget = budget; this.coalesceBytes = coalesceBytes; }

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

		private Threading::Mutex mutex;
		private Threading::ConditionVariable ready;
		private Core::Queue<OutItem*> items;
		private Core::HashMap<Core::String, int64> topicBytes;
		private int64 bytes;
		private int64 budget;
		private int64 coalesceBytes;
		private bool closed;
	}
}
