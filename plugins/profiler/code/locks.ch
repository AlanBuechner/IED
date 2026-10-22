// Locks (F-PRF-18, docs/16 "Locks"): which thread held a lock when, who waited for it meanwhile, and what that cost.
// A wait is time a thread was blocked; a hold is contended when another thread was waiting for the lock as it was
// released.

include "query.ch"

namespace Profiler
{
	enum class LockState { Held, Contended, Blocked }

	// The waits for one lock that touch a range, to tell which holds were contended.
	class LockWaits
	{
		public Core::Array<int64> starts;       // sorted
		public Core::Array<int64> latestEnd;    // the latest end among the waits up to each start

		public void Build(const Capture& capture, const LockInfo* lock, const Range& range);
		// Whether a thread was waiting at `t` (a wait that began before it and had not ended).
		public bool Covers(int64 t) const;
	}

	// When a hold ends: the capture's end while it lasts.
	int64 ReleaseOr(const LockSpan& span, int64 now);
	// The first hold of `holder` released at or after `t` (holds of one thread do not overlap, so their releases grow).
	int64 FirstReleasedAfter(const LockTrack* holder, int64 t, int64 now);
	// A lock's name: its own, else its site's, else "Lock <id>".
	Core::String LockName(const Capture& capture, const LockInfo* lock);
	// The lock's id, name, function, file and line.
	void WriteLockSite(IED::JsonWriter& json, const Capture& capture, const LockInfo* lock);

	enum class LockOrder { Wait, MaxWait, Contended, Hold, Acquisitions, Name }

	struct LockAsk
	{
		public int64 fromNs = -1;
		public int64 toNs = -1;
		public LockOrder order;
		public int32 limit = 200;
	}

	// Each lock's acquisitions, contended holds, waits and holds in a range, most waited for first (by default).
	Core::String Locks(const Capture& capture, const LockAsk& ask);
	// The busiest locks of the whole capture, for the summary.
	void WriteTopLocks(IED::JsonWriter& json, const Capture& capture, int32 most);
	// One hold of a lock by a thread (`index` into its holds; one past them, a wait still open): its wait and hold, the
	// threads that held the lock while it waited and those that waited while it held. Empty when there is none.
	Core::String LockHold(const Capture& capture, uint64 lockId, int32 track, int64 index);
}
