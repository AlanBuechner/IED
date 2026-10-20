// Thread: one thread of the program running a func<void()> (std::jthread). Thread::Run starts it attached to
// the runtime (the collector scans it, its thread_locals start); destroying a thread that is still joinable joins
// it. It cannot be copied: move it with take(). A zero Thread runs nothing.
//     Threading::Thread worker = Threading::Thread::Run([&total]() { total = Sum(); });
//     worker.Join();

namespace Threading
{
	class Thread
	{
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete() { Join(); }

		// Starts a thread running `body`; a zero Thread when the system starts none.
		public static Thread Run(func<void()> body);

		public bool IsJoinable() const { return handle != nullptr; }
		// Waits for the thread to end. Does nothing on a zero Thread.
		public void Join();
		// Lets the thread run on without this handle.
		public void Detach();

		public static uint64 CurrentId() { return thread_current_id(); }
		// The processors the system can run threads on, at least 1.
		public static int32 HardwareCount() { return (int32)thread_hardware_count(); }
		// Gives the rest of this thread's time slice to another thread that is ready to run.
		public static void Yield() { thread_yield(); }
		public static void Sleep(int64 nanoseconds) { thread_sleep_ns(nanoseconds); }

		private static void RunBody(void* body);

		private void* handle;   // the runtime's thread, not GC memory
	}
}
