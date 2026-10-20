// ConditionVariable: threads wait for a condition another thread makes true (std::condition_variable), over a
// sequence word they park on. Wait releases the mutex, sleeps until a notify, and takes the mutex again. A wait
// may also end without a notify, so wait in a loop that checks the condition. It cannot be copied.
//     LockGuard guard(mutex);
//     while (!ready) condition.Wait(mutex);

include "mutex.ch"

namespace Threading
{
	struct ConditionVariable
	{
		[operator("delete")] public void Delete() {}   // a place threads wait at: never copied

		// `mutex` is held by the caller, and is again when it returns.
		public void Wait(Mutex& mutex);
		// The same for at most `nanoseconds`; false when the time passed without a notify.
		public bool WaitFor(Mutex& mutex, int64 nanoseconds);
		public void NotifyOne();
		public void NotifyAll();

		private int32 sequence;
	}
}
