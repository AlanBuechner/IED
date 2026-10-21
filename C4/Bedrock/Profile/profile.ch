// Profile: instrumenting a program for the IED profiler (IED docs/16). A program run with IED_PROFILER set - the IED
// Profile task sets it to its listener ("127.0.0.1:<port>;token=<t>"), or "file:<path>" writes a trace file - streams
// its zones, frames, plots, messages, thread names, tasks, allocations and collections; without it every call costs one
// relaxed load. `include "profile.ch"` for the PROFILE_* macros, which -D PROFILE=0 compiles away.
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

	// A zone: from its creation to the end of its scope, on the calling thread.
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
	}

	// The end of a frame of the default frame set (call it once per presented frame), or of the set `name`.
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
	// profiler groups tasks by name. TaskBegin returns its id; 0 when profiling is off.
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
