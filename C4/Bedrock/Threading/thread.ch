// Thread: one thread of the program running a func<void()> (std::jthread). Thread::Run starts it attached to
// the runtime (the collector scans it, its thread_locals start); destroying a thread that is still joinable joins
// it. It cannot be copied: move it with take(). A zero Thread runs nothing.
//     Threading::Thread worker = Threading::Thread::Run([&total]() { total = Sum(); });
//     worker.Join();

include "Text/string.ch"

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
		// The logical processors this process may run threads on (its affinity), at least 1: a pool sized by it keeps no more threads busy than there are.
		public static int32 HardwareCount() { return (int32)thread_hardware_count(); }
		// The cores with a logical processor this process may run on, at least 1: PinCurrentToCore's numbers.
		public static int32 CoreCount() { return (int32)thread_core_count(); }
		// Gives the rest of this thread's time slice to another thread that is ready to run.
		public static void Yield() { thread_yield(); }
		// Sleeps on a high-resolution timer: it ends within about half a millisecond of `nanoseconds`, not on the system's tick.
		public static void Sleep(int64 nanoseconds) { thread_sleep_ns(nanoseconds); }
		// Names the calling thread for debuggers, profilers and crash dumps; false when the system cannot.
		public static bool SetCurrentName(const char* name) { return thread_set_name(name) == 0; }
		// Keeps the calling thread on one logical processor of core number `core` of those this process may run on (CoreCount), so threads pinned to
		// different cores never share one through its hyperthreads. False when there is no such core: the thread then runs where it did.
		public static bool PinCurrentToCore(int32 core) { return thread_pin_to_core(core) == 0; }
		// The calling thread's name, "" when it has none.
		public static Core::String CurrentName();

		private static void RunBody(void* body);

		private void* handle;   // the runtime's thread, not GC memory
	}
}
