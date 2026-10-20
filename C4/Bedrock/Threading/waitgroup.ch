// WaitGroup: a count of unfinished work (Go's sync.WaitGroup). Add before handing out work, Done as each piece
// finishes, Wait until the count is zero. Waiting spins a little, then parks on the count. It cannot be copied.
//     group.Add(2); ...; group.Wait();

namespace Threading
{
	struct WaitGroup
	{
		[operator("delete")] public void Delete() {}   // a place threads wait at: never copied

		public void Add(int64 count) { atomic_fetch_add(&remaining, count, MemoryOrder::AcqRel); }
		public void Done();
		public bool IsDone() const { return atomic_load(&remaining, MemoryOrder::Acquire) <= 0; }
		public int64 Remaining() const { return atomic_load(&remaining, MemoryOrder::Acquire); }
		public void Wait() const;
		// The same for at most `nanoseconds`; false when work remains.
		public bool WaitFor(int64 nanoseconds) const;

		private int64 remaining;
	}
}
