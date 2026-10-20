// Mutex: mutual exclusion in one word (std::mutex), with no OS lock: free, locked, or locked with sleepers. Lock
// takes a free mutex with one compare-exchange, spins a little while it is held, then parks on its word; Unlock
// wakes one sleeper only when there is one. Not recursive. It cannot be copied, and must not move while held.
// LockGuard holds a Mutex for its scope; UnlockGuard lets go of a held one for its scope (slow work inside a locked
// region that touches nothing the mutex guards) and takes it back at the end, an exception's included.
//     Threading::LockGuard guard(g_mutex);

namespace Threading
{
	struct Mutex
	{
		[operator("delete")] public void Delete() {}   // a mutex is a place threads wait at: never copied

		public void Lock();
		// Takes the mutex when it is free, without waiting.
		public bool TryLock();
		public void Unlock();

		private void LockContended();

		private int32 state;
	}

	struct LockGuard
	{
		[operator("create")] public void Init(Mutex& held) { mutex = &held; mutex->Lock(); }
		[operator("delete")] public void Delete() { mutex->Unlock(); }

		private Mutex* mutex;
	}

	struct UnlockGuard
	{
		[operator("create")] public void Init(Mutex& held) { mutex = &held; mutex->Unlock(); }
		[operator("delete")] public void Delete() { mutex->Lock(); }

		private Mutex* mutex;
	}
}
