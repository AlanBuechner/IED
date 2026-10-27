// Profile: instrumenting a program for the IED profiler (IED docs/16). A program run with IED_PROFILER set - the IED
// Profile task sets it to its listener ("127.0.0.1:<port>;token=<t>"), or "file:<path>" writes a trace file - streams
// its zones, frames, plots, messages, thread names, tasks, locks, allocations and collections; without it every call
// costs one relaxed load. `include "profile.ch"` for the PROFILE_* macros, which -D PROFILE=0 compiles away.
//     void Update() {
//         Profile::Zone zone("Update");             // ends when the scope does
//         Profile::Plot("Entities", entities.Count());
//     }
//     Profile::Frame();                            // once a frame, after presenting it

include "session.ch"

namespace Profile
{
	enum class Severity : int32 { Trace, Debug, Info, Warning, Error, Fatal }

	// Where a zone is: its name, file and line. A static one is registered with the profiler once per session.
	struct Site
	{
		[operator("create")] public void Init(const char* name, const char* file, int32 line) {
			this.name = name;
			this.file = file;
			this.line = line;
		}

		public const char* name;
		public const char* file;
		public int32 line;
		public uint64 id;                // its id in the session `session`
		public int64 session;
	}

	// The site's id in `session`, its SrcLocDef queued the first time.
	uint64 SiteId(Session* session, Site& site);

	// A zone: from its creation to the end of its scope, on the calling thread, with what the thread allocated in it
	// (the collector's own count, its nested zones' included) and the native memory it took and gave back (containers'
	// buffers).
	struct Zone
	{
		[operator("create")] public void Init(Site& site);
		// A zone named at run time (the name is looked up each time: prefer a static Site in hot code).
		[operator("create")] public void Init(Core::StringView name);
		[operator("delete")] public void Delete();

		// Text and a number shown with the zone (a file name, a count).
		public void Text(Core::StringView text);
		public void Value(uint64 value);

		private int64 session;
		private GC::AllocCounts allocated;      // the thread's count when the zone began
		private GC::AllocCounts profilerTook;   // and what the profiler had taken on it by then
	}

	// The end of a frame of the default frame set (call it once per presented frame), or of the set `name`; the
	// process's allocations so far go with it, so a frame's are the difference between its marks.
	void Frame();
	void Frame(Core::StringView name);
	// A discontinuous frame of the set `name` (a level load, a fixed-step tick).
	void FrameBegin(Core::StringView name);
	void FrameEnd(Core::StringView name);

	void Plot(Core::StringView name, double value);
	void Message(Core::StringView text, Severity severity = Severity::Info);
	// Names the calling thread from now on; call it again whenever the thread takes another role (a pooled worker
	// running "Physics") - the profiler keeps every name with the time it was given.
	void SetThreadName(Core::StringView name);

	// A task (IED docs/16 "by-task view"): a unit of work that may suspend on one thread and resume on another. The
	// profiler groups tasks by name. TaskBegin returns its id; 0 when profiling is off. While a session runs, every
	// JobSystem job is a task by itself, named as it was submitted ("Job" without a name).
	uint64 TaskBegin(Core::StringView name, uint64 parent = 0);
	void TaskEnd(uint64 task);
	void TaskSuspend(uint64 task);
	void TaskResume(uint64 task);

	// A task for a scope.
	struct TaskScope
	{
		[operator("create")] public void Init(Core::StringView name);
		[operator("delete")] public void Delete();

		public uint64 id;
	}

	// A lock as the profiler shows it (IED docs/16 "Locks"): its name and where it is declared. Registered once per
	// session, like a Site.
	struct LockSite
	{
		[operator("create")] public void Init(const char* name, const char* file, int32 line) {
			this.name = name;
			this.file = file;
			this.line = line;
		}

		public const char* name;
		public const char* file;
		public int32 line;
		public uint64 id;                // its id in the session `session`
		public int64 session;
	}

	// The calling thread's use of a lock of the program's own: it starts waiting for it, gets it, lets it go (`shared`
	// for a reader's hold of a reader-writer lock). A lock taken without waiting needs no LockWait.
	void LockWait(LockSite& lock, bool shared = false);
	void LockObtained(LockSite& lock, bool shared = false);
	void LockReleased(LockSite& lock, bool shared = false);

	// A Threading::Mutex the profiler watches: its waits and holds show on each thread's lock rows, its contention in
	// the Locks window. Taken without waiting when it is free (no wait is recorded then).
	struct Mutex
	{
		[operator("create")] public void Init(const char* name, const char* file, int32 line) { site = LockSite(name, file, line); }
		[operator("delete")] public void Delete() {}

		public void Lock();
		public bool TryLock();
		public void Unlock();

		public LockSite site;
		private Threading::Mutex inner;
	}

	// Holds a Profile::Mutex for its scope.
	struct MutexGuard
	{
		[operator("create")] public void Init(Mutex& held) { mutex = &held; mutex->Lock(); }
		[operator("delete")] public void Delete() { mutex->Unlock(); }

		private Mutex* mutex;
	}

	// Memory of a pool of the program's own (an arena, a pool allocator): what it hands out and takes back. Pool 0 is
	// the program's default heap. The GC's own heap is reported by itself (collections and its size).
	void Alloc(const void* address, int64 size, uint32 pool = 0);
	void Free(const void* address, uint32 pool = 0);
	void PoolName(uint32 pool, Core::StringView name);
	void PoolReset(uint32 pool);

	// Whether events are being recorded now.
	bool IsActive();
	// Starts recording to `target` ("host:port;token=<t>" or "file:<path>") without IED_PROFILER; Stop ends it. Both
	// are for tools and tests: a program run by IED is started by its environment.
	bool Start(Core::StringView target, Core::String& problem);
	void Stop();
}
