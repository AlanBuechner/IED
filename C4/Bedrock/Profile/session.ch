// A profiling session (IED docs/16 "Client library requirements"): the connection to the IED profiler (or a trace
// file), each thread's buffer of encoded events, the global buffer of definitions, memory and GC events, and the
// flusher thread that sends them every 10 ms - the thread buffers taken first, then the global one sent first, so a
// definition always arrives before the event that names it.
//
// One session runs per process, so its state is global: the one exception to Bedrock's rule, as a profiler is the
// process's by nature. A program that never profiles pays one relaxed load per event site.

include "encode.ch"
include "Threading/mutex.ch"
include "Threading/thread.ch"
include "Net/tcp.ch"
include "FS/file.ch"
include "Container/hashmap.ch"
include "Text/string.ch"

namespace Profile
{
	const int64 kFlushEveryNs = 10000000;
	const int64 kCalibrateEveryNs = 1000000000;
	const int64 kHeapPlotEveryNs = 100000000;
	const int32 kConnectWaitMs = 2000;
	const int32 kSendWaitMs = 5000;
	const uint64 kEstimatedClock = 1;

	// Events since the flusher last took them, each time a delta from the one before; `base` is the time the first
	// delta of these bytes is from (the last time when the flusher took the ones before).
	class EventBuffer
	{
		public Threading::Mutex lock;
		public Core::Array<uint8> bytes;
		public int64 base;
		public int64 last;
		public uint64 thread;            // the client's id of the thread; 0 for the global buffer
		public int32 ended;              // the thread has exited (atomic)

		// Starts a timed event: its type and the delta from the event before. The lock is held.
		public void Timed(Event type, int64 ticks);
	}

	class Session
	{
		public int64 number;             // sites registered in an earlier session register again
		public int64 frequency;          // ticks a second
		public int64 startTicks;
		public Net::TcpStream* stream;
		public FS::File* file;
		public Threading::Mutex lock;    // what follows, up to the buffers
		public Core::HashMap<Core::String, uint64> strings;
		public Core::HashMap<Core::String, uint64> namedSites;
		public Core::HashMap<Core::String, uint64> plots;
		public uint64 nextString;
		public uint64 nextSite;
		public uint64 nextPlot;
		public uint64 nextThread;
		public uint64 nextTask;
		public Core::Array<EventBuffer*> threads;
		public EventBuffer global;
		public int32 stopping;           // atomic
		public Threading::Thread* flusher;
		public int64 lastCalibration;
		public int64 lastHeapPlot;
		public uint64 heapPlot;

		// The id of `text` (0 for empty), its StringDef queued the first time. The lock is held.
		public uint64 Intern(Core::StringView text);
		// A fresh id for text sent once (a message, a zone's text): never kept. The lock is held.
		public uint64 Once(Core::StringView text);
		// Writes the bytes to the trace's sink; false when it failed (the session then ends).
		public bool Send(const Core::Array<uint8>& bytes);
	}

	// The running session, or null; turns profiling on from IED_PROFILER the first time it is asked.
	Session* Running();
	// The calling thread's buffer in `session`, made (and its ThreadDef queued) on first use.
	EventBuffer* ThreadBuffer(Session* session);
	// The processor's time stamp counter: the clock every event is stamped with.
	int64 Now();

	// Starts a session to `target` - "host:port;token=<t>" (an IED profiler), or "file:<path>" (a trace file) - for
	// the program named `app`. False with `problem` when the target cannot be reached or opened.
	bool StartSession(Core::StringView target, Core::StringView app, Core::String& problem);
	// Sends what is buffered and Bye, and closes the session; events after it are dropped.
	void StopSession();
}
