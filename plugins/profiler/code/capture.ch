// A capture as the server holds it (docs/16): everything one connection (or one trace file) said, indexed for the
// queries the timeline, the analysis windows and the AI tools ask. Times are ticks since the capture started; the
// Clock turns them into nanoseconds. Names stay ids until a query reads them, so a definition that arrives late still
// names everything that used it.
//
// Threads: the capture's own lock guards it; ingest takes it per chunk, queries for their whole answer.

include "zones.ch"

namespace Profiler
{
	const uint64 kEstimatedClockFlag = 1;
	const int64 kMostWarnings = 1000;

	// Ticks to nanoseconds: the client's frequency, refined from its Calibration pairs when it said it estimated it.
	struct Clock
	{
		public int64 startTicks;
		public double nsPerTick = 1.0;

		public int64 Ns(int64 ticks) const { return (int64)((double)ticks * nsPerTick); }
		public int64 Ticks(int64 ns) const { return (int64)((double)ns / nsPerTick); }
	}

	struct SrcLoc
	{
		public uint64 nameStr;
		public uint64 functionStr;
		public uint64 fileStr;
		public uint64 line;
		public uint32 color;
	}

	// One thread's lane: its zones by depth and its names over time.
	class ThreadTrack
	{
		public uint64 clientId;
		public uint64 osId;
		public uint64 groupStr;
		public Core::Array<int64> nameTimes;
		public Core::Array<uint64> nameStrs;
		public Core::Array<ZoneLevel*> levels;
		public Core::Array<ZoneExtra> extras;
		public Core::Array<OpenZone> open;
		public int32 overflow;              // zones begun deeper than kMostDepth, not kept
		public int64 zones;
		public int64 firstTicks = -1;
		public int64 lastTicks;

		// The zone the next ZoneText, ZoneValue, ... describes: the innermost open one.
		public ZoneExtra* InnermostExtra();
		// The name it had at `t`: 0 before its first.
		public uint64 NameAt(int64 t) const;
	}

	// Totals of one source location over the whole capture, and where each of its zones is.
	class SiteStats
	{
		public int64 count;
		public int64 total;
		public int64 self;
		public int64 most;
		public int64 least = -1;
		public Core::Array<uint64> refs;    // ZoneRef bits
	}

	class FrameSet
	{
		public uint64 nameStr;
		public bool discontinuous;
		public int32 track = -1;            // the thread that marked its first frame; -1 for the process
		public Core::Array<int64> starts;
		public Core::Array<int64> ends;     // kOpen for the frame still running

		public int64 Completed() const;
	}

	class Plot
	{
		public uint64 id;
		public uint64 nameStr;
		public uint64 kind;                 // 0 number, 1 memory, 2 percent
		public Core::Array<int64> times;
		public Core::Array<double> values;
	}

	struct MessageRecord
	{
		public int64 t;
		public int32 track;
		public int32 severity;
		public uint64 textStr;
		public uint32 color;
	}

	struct Allocation
	{
		public int64 t;
		public int64 freed = kOpen;
		public uint64 address;
		public int64 size;
		public int32 track;
		public uint64 callstack;
		public uint64 pool;
	}

	class Pool
	{
		public uint64 id;
		public uint64 nameStr;
		public Core::HashMap<uint64, int64> live;   // address -> allocation index
		public Core::Array<int64> times;            // the pool's bytes in use after each change
		public Core::Array<int64> bytes;
		public int64 inUse;
		public int64 peak;
		public int64 allocs;
		public int64 frees;
		public int64 earlierFrees;                  // frees of blocks allocated before the capture began
	}

	// What the process allocated so far, at the times a client counted it (AllocCount): for clients that count
	// allocations instead of sending each one.
	class AllocCounter
	{
		public Core::Array<int64> times;
		public Core::Array<int64> objects;
		public Core::Array<int64> bytes;
	}

	struct CensusType
	{
		public uint64 nameStr;
		public int64 objects;
		public int64 bytes;
	}

	// Live objects by type after one collection (HeapCensus and its HeapCensusType lines).
	class HeapCensus
	{
		public int64 t;
		public int64 expected;
		public Core::Array<CensusType> types;
	}

	struct GcRecord
	{
		public int64 begin;
		public int64 end = kOpen;
		public int32 kind;
		public int32 generation;
		public int64 bytesBefore;
		public int64 bytesAfter;
		public int64 objectsFreed;
		public int64 pinned;
		public int64 roots;

		// Bytes the collection reclaimed; -1 when the client did not say what the heap held before (bytesBefore 0).
		public int64 Freed() const { return bytesBefore == 0 ? -1 : (bytesBefore > bytesAfter ? bytesBefore - bytesAfter : 0); }
	}

	// The intervals in which tasks of one name ran on one thread, in time order.
	class TaskRuns
	{
		public int32 track;
		public Core::Array<int64> starts;
		public Core::Array<int64> ends;     // kOpen while it runs
		public Core::Array<uint64> tasks;
	}

	class TaskInfo
	{
		public uint64 id;
		public uint64 nameStr;
		public uint64 parent;
		public int64 begin;
		public int64 end = kOpen;
		public TaskRuns* running;           // where its current interval is, while it runs
		public int64 runningEntry;
		public int64 runs;
	}

	// Every interval in which a task of one name ran (docs/16 "by-task view"), kept per thread.
	class TaskLane
	{
		public uint64 nameStr;
		public Core::HashMap<int32, TaskRuns*> runsOf;
		public Core::Array<TaskRuns*> runs;
		public int64 count;
		public int64 totalTicks;
	}

	// One hold of a lock by a thread: when it asked (wait; the obtain when it never waited), got it and let it go.
	struct LockSpan
	{
		public int64 wait;
		public int64 obtain;
		public int64 release = kOpen;
		public bool shared;
	}

	// A lock's holds by one thread in time order: a thread's events arrive in order, and an obtain while it already
	// holds the lock (a recursive lock) only deepens the hold.
	class LockTrack
	{
		public int32 track;
		public Core::Array<LockSpan> spans;
		public int64 waitingSince = -1;     // a wait not yet obtained (a deadlock, when it never is)
		public int32 depth;                 // obtains not yet released
	}

	class LockInfo
	{
		public uint64 id;
		public uint64 nameStr;
		public uint64 site;
		public Core::HashMap<int32, LockTrack*> trackOf;
		public Core::Array<LockTrack*> tracks;
		public int64 acquisitions;
	}

	struct GpuZone
	{
		public int64 cpuBegin;
		public int64 cpuEnd = kOpen;
		public uint64 beginQuery;
		public uint64 endQuery;
		public uint32 site;
		public int32 depth;
		public int32 track;
	}

	class GpuContext
	{
		public uint64 id;
		public uint64 api;
		public uint64 nameStr;
		public double periodPs = 1.0;
		public Core::Array<int64> calibrationCpu;
		public Core::Array<int64> calibrationGpu;
		public Core::HashMap<uint64, int64> times;   // query id -> GPU ticks
		public Core::Array<GpuZone> zones;
		public Core::Array<int64> open;               // indices of zones whose end has not been recorded

		// A GPU timestamp on the CPU's timeline (ticks since the capture started); false without a calibration.
		public bool ToCpu(int64 gpuTicks, const Clock& clock, int64 cpuFreqHz, int64& out) const;
	}

	struct ModuleRecord
	{
		public uint64 base;
		public uint64 size;
		public uint64 pathStr;
		public uint64 debugIdStr;
	}

	struct SymbolRecord
	{
		public uint64 functionStr;
		public uint64 fileStr;
		public uint64 line;
	}

	struct SampleRecord
	{
		public int64 t;
		public int32 track;
		public uint64 callstack;
		public int32 kind;                  // 0 CPU time in ns, 1 sample count, 2 bytes allocated, 3 objects allocated, 4 other
		public int64 weight;
	}

	struct WarningRecord
	{
		public int64 t;
		public Core::String text;
	}

	class Capture
	{
		public Threading::Mutex lock;

		// From Hello.
		public bool hello;
		public Core::String appName;
		public uint64 pid;
		public int64 clockFreqHz;
		public int64 startUnixNs;
		public uint64 flags;
		public Clock clock;
		public Core::Array<int64> calibrationTicks;
		public Core::Array<int64> calibrationUnixNs;

		// What came after.
		public int64 lastTicks;
		public int64 events;
		public int64 bytes;
		public bool ended;                  // Bye arrived (or the connection closed)
		public uint64 byeReason;
		public uint64 eventsDropped;

		public Core::HashMap<uint64, Core::String> strings;
		public Core::HashMap<uint64, SrcLoc> srclocs;
		public Core::HashMap<uint64, int32> trackOf;        // client thread id -> index in tracks
		public Core::Array<ThreadTrack*> tracks;
		public Core::HashMap<uint64, SiteStats*> sites;
		public Core::HashMap<uint64, FrameSet*> frameSetOf;
		public Core::Array<FrameSet*> frameSets;
		public Core::HashMap<uint64, Plot*> plotOf;
		public Core::Array<Plot*> plots;
		public Core::Array<MessageRecord> messages;
		public Core::HashMap<uint64, Pool*> poolOf;
		public Core::Array<Pool*> pools;
		public Core::Array<Allocation> allocations;
		public Core::Array<GcRecord> gcs;
		public AllocCounter allocCounter;
		public Core::Array<HeapCensus*> censuses;
		public int64 zoneAllocs;                             // zones that said what they allocated (ZoneAllocs, ZoneNative)
		public Core::HashMap<uint64, TaskInfo*> tasks;
		public Core::HashMap<uint64, TaskLane*> taskLaneOf;  // task name string id -> lane
		public Core::Array<TaskLane*> taskLanes;
		public Core::HashMap<uint64, LockInfo*> locks;
		public Core::Array<LockInfo*> lockList;              // in the order they appeared
		public Core::HashMap<uint64, GpuContext*> gpuOf;
		public Core::Array<GpuContext*> gpus;
		public Core::Array<ModuleRecord> modules;
		public Core::HashMap<uint64, Core::Array<uint64>> callstacks;
		public Core::HashMap<uint64, SymbolRecord> symbols;
		public Core::Array<SampleRecord> samples;
		public Core::Array<WarningRecord> warnings;
		public int64 warningCount;

		// The text of a string id ("" for 0 or one not defined).
		public Core::StringView Text(uint64 id) const;
		// A source location's display name: its name, else its function, else "?".
		public Core::StringView SiteName(uint64 site) const;
		// A zone's name as the timeline shows it: its per-call name when it has one, else its site's.
		public Core::StringView ZoneName(const ThreadTrack* track, const ZoneLevel* level, int64 index) const;
		public void Warn(int64 t, Core::StringView text);
		// Nanoseconds the capture covers so far.
		public int64 DurationNs() const { return clock.Ns(lastTicks); }
		public int64 ZoneCount() const;
	}
}
