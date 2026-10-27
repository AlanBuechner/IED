// A capture as the server holds it (docs/16): everything one connection (or one trace file) said, indexed for the
// queries the timeline, the analysis windows and the AI tools ask. Times are ticks since the capture started; the
// Clock turns them into nanoseconds. Names stay ids until a query reads them, so a definition that arrives late still
// names everything that used it.
//
// Threads: the capture's own lock guards it; ingest takes it per chunk, queries for their whole answer.

include "analysis/Zones.ch"

namespace Profiler
{
	const uint64 c_EstimatedClockFlag = 1;
	const int64 c_MostWarnings = 1000;

	// Ticks to nanoseconds: the client's frequency, refined from its Calibration pairs when it said it estimated it.
	struct Clock
	{
		public int64 m_StartTicks;
		public double m_NsPerTick = 1.0;

		public int64 Ns(int64 ticks) const { return (int64)((double)ticks * m_NsPerTick); }
		public int64 Ticks(int64 ns) const { return (int64)((double)ns / m_NsPerTick); }
	}

	struct SrcLoc
	{
		public uint64 m_NameStr;
		public uint64 m_FunctionStr;
		public uint64 m_FileStr;
		public uint64 m_Line;
		public uint32 m_Color;
	}

	// One thread's lane: its zones by depth and its names over time.
	class ThreadTrack
	{
		public uint64 m_ClientId;
		public uint64 m_OsId;
		public uint64 m_GroupStr;
		public Core::Array<int64> m_NameTimes;
		public Core::Array<uint64> m_NameStrs;
		public Core::Array<ZoneLevel*> m_Levels;
		public Core::Array<ZoneExtra> m_Extras;
		public Core::Array<OpenZone> m_Open;
		public int32 m_Overflow;              // zones begun deeper than kMostDepth, not kept
		public int64 m_Zones;
		public int64 m_FirstTicks = -1;
		public int64 m_LastTicks;

		// The zone the next ZoneText, ZoneValue, ... describes: the innermost open one.
		public ZoneExtra* InnermostExtra();
		// The name it had at `t`: 0 before its first.
		public uint64 NameAt(int64 t) const;
	}

	// Totals of one source location over the whole capture, and where each of its zones is.
	class SiteStats
	{
		public int64 m_Count;
		public int64 m_Total;
		public int64 m_Self;
		public int64 m_Most;
		public int64 m_Least = -1;
		public Core::Array<uint64> m_Refs;    // ZoneRef bits
	}

	class FrameSet
	{
		public uint64 m_NameStr;
		public bool m_Discontinuous;
		public int32 m_Track = -1;            // the thread that marked its first frame; -1 for the process
		public Core::Array<int64> m_Starts;
		public Core::Array<int64> m_Ends;     // kOpen for the frame still running

		public int64 Completed() const;
	}

	class Plot
	{
		public uint64 m_Id;
		public uint64 m_NameStr;
		public uint64 m_Kind;                 // 0 number, 1 memory, 2 percent
		public Core::Array<int64> m_Times;
		public Core::Array<double> m_Values;
	}

	struct MessageRecord
	{
		public int64 m_T;
		public int32 m_Track;
		public int32 m_Severity;
		public uint64 m_TextStr;
		public uint32 m_Color;
	}

	struct Allocation
	{
		public int64 m_T;
		public int64 m_Freed = c_Open;
		public uint64 m_Address;
		public int64 m_Size;
		public int32 m_Track;
		public uint64 m_Callstack;
		public uint64 m_Pool;
	}

	class Pool
	{
		public uint64 m_Id;
		public uint64 m_NameStr;
		public Core::HashMap<uint64, int64> m_Live;   // address -> allocation index
		public Core::Array<int64> m_Times;            // the pool's bytes in use after each change
		public Core::Array<int64> m_Bytes;
		public int64 m_InUse;
		public int64 m_Peak;
		public int64 m_Allocs;
		public int64 m_Frees;
		public int64 m_EarlierFrees;                  // frees of blocks allocated before the capture began
	}

	// What the process allocated so far, at the times a client counted it (AllocCount): for clients that count
	// allocations instead of sending each one.
	class AllocCounter
	{
		public Core::Array<int64> m_Times;
		public Core::Array<int64> m_Objects;
		public Core::Array<int64> m_Bytes;
	}

	struct CensusType
	{
		public uint64 m_NameStr;
		public int64 m_Objects;
		public int64 m_Bytes;
		public int64 m_NativeBytes;   // the native memory its objects hold (HeapCensusTypeNative; 0 from HeapCensusType)
	}

	// Live objects by type after one collection (HeapCensus and its HeapCensusType lines).
	class HeapCensus
	{
		public int64 m_T;
		public int64 m_Expected;
		public Core::Array<CensusType> m_Types;
	}

	struct GcRecord
	{
		public int64 m_Begin;
		public int64 m_End = c_Open;
		public int32 m_Kind;
		public int32 m_Generation;
		public int64 m_BytesBefore;
		public int64 m_BytesAfter;
		public int64 m_ObjectsFreed;
		public int64 m_Pinned;
		public int64 m_Roots;

		// Bytes the collection reclaimed; -1 when the client did not say what the heap held before (bytesBefore 0).
		public int64 Freed() const { return m_BytesBefore == 0 ? -1 : (m_BytesBefore > m_BytesAfter ? m_BytesBefore - m_BytesAfter : 0); }
	}

	// The intervals in which tasks of one name ran on one thread, in time order.
	class TaskRuns
	{
		public int32 m_Track;
		public Core::Array<int64> m_Starts;
		public Core::Array<int64> m_Ends;     // kOpen while it runs
		public Core::Array<uint64> m_Tasks;
	}

	class TaskInfo
	{
		public uint64 m_Id;
		public uint64 m_NameStr;
		public uint64 m_Parent;
		public int64 m_Begin;
		public int64 m_End = c_Open;
		public TaskRuns* m_Running;           // where its current interval is, while it runs
		public int64 m_RunningEntry;
		public int64 m_Runs;
	}

	// Every interval in which a task of one name ran (docs/16 "by-task view"), kept per thread.
	class TaskLane
	{
		public uint64 m_NameStr;
		public Core::HashMap<int32, TaskRuns*> m_RunsOf;
		public Core::Array<TaskRuns*> m_Runs;
		public int64 m_Count;
		public int64 m_TotalTicks;
	}

	// One hold of a lock by a thread: when it asked (wait; the obtain when it never waited), got it and let it go.
	struct LockSpan
	{
		public int64 m_Wait;
		public int64 m_Obtain;
		public int64 m_Release = c_Open;
		public bool m_Shared;
	}

	// A lock's holds by one thread in time order: a thread's events arrive in order, and an obtain while it already
	// holds the lock (a recursive lock) only deepens the hold.
	class LockTrack
	{
		public int32 m_Track;
		public Core::Array<LockSpan> m_Spans;
		public int64 m_WaitingSince = -1;     // a wait not yet obtained (a deadlock, when it never is)
		public int32 m_Depth;                 // obtains not yet released
	}

	class LockInfo
	{
		public uint64 m_Id;
		public uint64 m_NameStr;
		public uint64 m_Site;
		public Core::HashMap<int32, LockTrack*> m_TrackOf;
		public Core::Array<LockTrack*> m_Tracks;
		public int64 m_Acquisitions;
	}

	const int64 c_NoGpuTime = -1;

	// A zone on a GPU queue: when its thread recorded it (CPU ticks) and, once read back, its timestamps (GPU ticks).
	struct GpuZone
	{
		public int64 m_CpuBegin;
		public int64 m_CpuEnd = c_Open;
		public uint64 m_BeginQuery;
		public uint64 m_EndQuery;
		public int64 m_GpuBegin = c_NoGpuTime;
		public int64 m_GpuEnd = c_NoGpuTime;
		public uint32 m_Site;
		public int32 m_Depth;              // among its thread's open zones on the context
		public int32 m_Track;
	}

	// A GPU queue. Query ids are a client's query-heap slots, reused round a ring, so a time belongs to the zone waiting
	// for its slot when it arrives - or, sent before that zone (another thread's chunk of the same flush), to the next
	// zone that takes the slot. Threads recording into it at once nest their zones apart.
	class GpuContext
	{
		public uint64 m_Id;
		public uint64 m_Api;
		public uint64 m_NameStr;
		public double m_PeriodPs = 1.0;
		public Core::Array<int64> m_CalibrationCpu;
		public Core::Array<int64> m_CalibrationGpu;
		public Core::Array<GpuZone> m_Zones;
		public Core::HashMap<int32, Core::Array<int64>> m_Open;   // per track: the zones begun there and not ended
		public Core::HashMap<uint64, int64> m_Waiting;            // a slot -> the zone whose time it will hold
		public Core::HashMap<uint64, int64> m_Early;              // a slot -> a time no zone waited for yet

		// A GPU timestamp on the CPU's timeline (ticks since the capture started); false without a calibration.
		public bool ToCpu(int64 gpuTicks, const Clock& clock, int64 cpuFreqHz, int64& out) const;
		// Where a zone ran on the CPU's timeline; false until both its times are back and the context is calibrated.
		public bool Span(const GpuZone& zone, const Clock& clock, int64 cpuFreqHz, int64& begin, int64& end) const;
	}

	struct ModuleRecord
	{
		public uint64 m_Base;
		public uint64 m_Size;
		public uint64 m_PathStr;
		public uint64 m_DebugIdStr;
	}

	struct SymbolRecord
	{
		public uint64 m_FunctionStr;
		public uint64 m_FileStr;
		public uint64 m_Line;
	}

	struct SampleRecord
	{
		public int64 m_T;
		public int32 m_Track;
		public uint64 m_Callstack;
		public int32 m_Kind;                  // 0 CPU time in ns, 1 sample count, 2 bytes allocated, 3 objects allocated, 4 other
		public int64 m_Weight;
	}

	struct WarningRecord
	{
		public int64 m_T;
		public Core::String m_Text;
	}

	class Capture
	{
		public Threading::Mutex m_Lock;

		// From Hello.
		public bool m_Hello;
		public Core::String m_AppName;
		public uint64 m_Pid;
		public int64 m_ClockFreqHz;
		public int64 m_StartUnixNs;
		public uint64 m_Flags;
		public Clock m_Clock;
		public Core::Array<int64> m_CalibrationTicks;
		public Core::Array<int64> m_CalibrationUnixNs;

		// What came after.
		public int64 m_LastTicks;
		public int64 m_Events;
		public int64 m_Bytes;
		public bool m_Ended;                  // Bye arrived (or the connection closed)
		public uint64 m_ByeReason;
		public uint64 m_EventsDropped;

		public Core::HashMap<uint64, Core::String> m_Strings;
		public Core::HashMap<uint64, SrcLoc> m_Srclocs;
		public Core::HashMap<uint64, int32> m_TrackOf;        // client thread id -> index in tracks
		public Core::Array<ThreadTrack*> m_Tracks;
		public Core::HashMap<uint64, SiteStats*> m_Sites;
		public Core::HashMap<uint64, FrameSet*> m_FrameSetOf;
		public Core::Array<FrameSet*> m_FrameSets;
		public Core::HashMap<uint64, Plot*> m_PlotOf;
		public Core::Array<Plot*> m_Plots;
		public Core::Array<MessageRecord> m_Messages;
		public Core::HashMap<uint64, Pool*> m_PoolOf;
		public Core::Array<Pool*> m_Pools;
		public Core::Array<Allocation> m_Allocations;
		public Core::Array<GcRecord> m_Gcs;
		public AllocCounter m_AllocCounter;
		public Core::Array<HeapCensus*> m_Censuses;
		public Core::String m_SourceRoot;                      // its workspace's folder: a source file under it is named relative to it
		public int64 m_ZoneAllocs;                             // zones that said what they allocated (ZoneAllocs, ZoneNative, ZoneNativeFreed)
		public bool m_CensusNative;                            // a census said what native memory its types hold
		public Core::HashMap<uint64, TaskInfo*> m_Tasks;
		public Core::HashMap<uint64, TaskLane*> m_TaskLaneOf;  // task name string id -> lane
		public Core::Array<TaskLane*> m_TaskLanes;
		public Core::HashMap<uint64, LockInfo*> m_Locks;
		public Core::Array<LockInfo*> m_LockList;              // in the order they appeared
		public Core::HashMap<uint64, GpuContext*> m_GpuOf;
		public Core::Array<GpuContext*> m_Gpus;
		public Core::Array<ModuleRecord> m_Modules;
		public Core::HashMap<uint64, Core::Array<uint64>> m_Callstacks;
		public Core::HashMap<uint64, SymbolRecord> m_Symbols;
		public Core::Array<SampleRecord> m_Samples;
		public Core::Array<WarningRecord> m_Warnings;
		public int64 m_WarningCount;

		// The text of a string id ("" for 0 or one not defined).
		public Core::StringView Text(uint64 id) const;
		// A source file's name as the tools show it: relative to sourceRoot when the file is under it (a debug
		// information's absolute path), else as the client named it.
		public Core::String SourceText(uint64 fileStr) const;
		// A source location's display name: its name, else its function, else "?".
		public Core::StringView SiteName(uint64 site) const;
		// A zone's name as the timeline shows it: its per-call name when it has one, else its site's.
		public Core::StringView ZoneName(const ThreadTrack* track, const ZoneLevel* level, int64 index) const;
		public void Warn(int64 t, Core::StringView text);
		// Nanoseconds the capture covers so far.
		public int64 DurationNs() const { return m_Clock.Ns(m_LastTicks); }
		public int64 ZoneCount() const;
	}
}
