// EventLoop: one thread's loop over sockets, timers and work posted from other threads (D-70). Run calls each
// callback on the thread running the loop, one at a time: a socket's when it is ready for what its watch waits for, a
// timer's when it is due, posted work in the order it was posted. Post and Stop are safe from any thread; every other
// call belongs to the loop's thread (or comes before Run). A callback that throws ends Run with the exception, the
// loop left as it was, ready to run again. Blocking work belongs on another thread, which Posts its result back.
//     Net::EventLoop loop;
//     loop.Watch(listener.Socket(), Net::kReadable, [&listener](int32 ready) { Serve(listener.Accept(0)); });
//     loop.Every(1000, []() { printf("tick\n"); });
//     loop.Run();

include "poll.ch"
include "udp.ch"
include "Container/hashmap.ch"
include "Container/queue.ch"
include "Threading/mutex.ch"
include "Threading/atomic.ch"

namespace Net
{
	struct LoopWatch
	{
		public int64 socket;
		public int32 wanted;
		public func<void(int32)> callback;
	}

	struct LoopTimer
	{
		public int64 dueNs;
		public int64 periodNs;   // 0: once
		public func<void()> callback;
	}

	// A timer in the loop's queue: the earliest first, then the first made.
	struct DueTimer
	{
		public int64 dueNs;
		public int64 id;
	}

	class EventLoop
	{
		// Open, or closed with Error() when its wake socket cannot be made.
		[operator("create")] public void Init();
		[operator("delete")] public void Delete() { Close(); }

		public bool IsOpen() const { return wake.IsOpen(); }
		public const char* Error() const { return error.CStr(); }

		// Calls `callback` with what `socket` is ready for (kReadable, kWritable, and kClosed or kFailed whether waited
		// for or not) each turn it is, until Unwatch. A watch's id, 0 for a socket that is not one. A socket watched for
		// kWritable is ready nearly always: change the watch once there is nothing left to write.
		public int64 Watch(int64 socket, int32 wanted, func<void(int32)> callback);
		// What a watch waits for; 0 pauses it. False for an id that is not watched.
		public bool Change(int64 watch, int32 wanted);
		public bool Unwatch(int64 watch);
		public int64 WatchCount() const { return watches.Count(); }

		// Calls `callback` once, `delayMs` from now; a timer's id.
		public int64 After(int64 delayMs, func<void()> callback);
		// Calls `callback` every `periodMs` (at least 1) until Cancel. A turn that comes late runs it once, not once
		// for each period missed.
		public int64 Every(int64 periodMs, func<void()> callback);
		// Stops a timer that has not run (or a periodic one); false when there is no such timer.
		public bool Cancel(int64 timer);

		// Runs `work` on the loop's thread in a coming turn. Safe from any thread; false once the loop is closed.
		public bool Post(func<void()> work);
		// Takes turns until Stop. A Stop before Run makes it return at once.
		public void Run();
		// One turn: waits up to `timeoutMs` (kWaitForever, or less when a timer is due sooner or work is posted) for a
		// socket to be ready, then calls the ready sockets', due timers' and posted work's callbacks. How many ran.
		public int32 RunOnce(int32 timeoutMs);
		// Makes Run return once the turn in progress ends. Safe from any thread.
		public void Stop();
		// Drops every watch, timer and posted work, and closes the wake sockets. Idempotent.
		public void Close();

		private int32 WaitFor(int32 timeoutMs);
		private int32 RunReadySockets(Core::Array<PollEntry>& entries, Core::Array<int64>& ids);
		private int32 RunDueTimers();
		private int32 RunPosted();
		private void Drain();
		private void Wake();   // with postLock held

		private Core::HashMap<int64, LoopWatch> watches;
		private Core::HashMap<int64, LoopTimer> timers;
		private Core::Array<DueTimer> due;   // a binary heap
		private int64 nextId;

		private Threading::Mutex postLock;            // guards posted, waker and closed
		private Core::Queue<func<void()>> posted;
		private UdpSocket waker;                      // posting threads send a byte to `wake` to end a wait
		private bool closed;
		private Threading::Atomic<bool> woken;        // a byte is on its way: no need to send another
		private Threading::Atomic<bool> stopping;
		private UdpSocket wake;
		private Address wakeAddress;
		private Core::String error;
	}
}
