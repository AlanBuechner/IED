// The analyses behind the AI's investigation tools (docs/13 T-PRF-13 to T-PRF-20, docs/16 "Finding problems"): where
// a program allocates, how its memory moves over time, its counters, why its slow frames are slow, how busy its
// threads are and what happened in a window. Each takes the capture (its lock held by the caller) and answers JSON,
// which the prof.* methods return and the profile_* tools turn into text.

include "Analysis.ch"
include "Locks.ch"

namespace Profiler
{
	// What allocations are read from: the best the capture has (events, else a program's counts - its sampled call
	// stacks when grouped by call stack or function - else samples), or the one named.
	enum class AllocSource { Best, Events, Zones, Samples }

	struct AllocAsk
	{
		public int64 m_FromNs = -1;
		public int64 m_ToNs = -1;
		public AllocSource m_Source;
		public AllocGroup m_GroupBy;
		public bool m_ByBytes;             // the most bytes first instead of the most allocations
		public int32 m_Limit = 50;
	}

	struct HitchAsk
	{
		public int64 m_ThresholdNs = -1;   // a frame longer is slow; twice the median when left out
		public int32 m_Limit = 10;
	}

	struct WindowAsk
	{
		public int64 m_FromNs = -1;
		public int64 m_ToNs = -1;
		public int32 m_Depth = 2;          // zones this deep and shallower
		public Core::String m_Thread;      // threads whose name holds this text (any case); empty for all
		public int32 m_Limit = 400;        // entries in all
	}

	// Allocations in a range by group: counts and bytes in all and per frame, churn (freed within the frame they were
	// made in), what is still live at the range's end; the range's allocations per frame. From allocation samples
	// (kind bytes or objects) by function when the capture has no allocation events.
	Core::String Allocations(const Capture& capture, const AllocAsk& ask);
	// Each pool and memory plot at `points` evenly spaced times: growth, peak, the lowest it got in each step (its
	// floor); collections, how often, their pauses and how many fell in slow frames.
	Core::String MemoryTrend(const Capture& capture, int64 fromNs, int64 toNs, int32 points);
	// The program's counters (plots that are not memory), or the one named: min, mean, max, last and values at points.
	Core::String Plots(const Capture& capture, Core::StringView name, int64 fromNs, int64 toNs, int32 points);
	// The slow frames and what made each slow, then the causes counted over all of them.
	Core::String Hitches(const Capture& capture, const HitchAsk& ask);
	// Each thread's busy time, idle gaps, lock waits and costliest zones in a range; how many were busy at once.
	Core::String Threads(const Capture& capture, int64 fromNs, int64 toNs);
	// What happened in a window, per thread in time order.
	Core::String Window(const Capture& capture, const WindowAsk& ask);

	// What a zone counted (ZoneAllocs, ZoneNative, ZoneNativeFreed): objects and their bytes, and the native memory its
	// thread took and gave back.
	struct ZoneCounts
	{
		public int64 m_Objects;
		public int64 m_Bytes;
		public int64 m_NativeBytes;
		public int64 m_NativeFreed;

		public bool IsEmpty() const { return m_Objects == 0 && m_Bytes == 0 && m_NativeBytes == 0 && m_NativeFreed == 0; }
	}

	// What zone `i` of `level` counted, its children's included.
	ZoneCounts ZoneCountsOf(const ThreadTrack* track, const ZoneLevel* level, int64 i);
	// What each zone counted itself (its counts less its children's), for the zones starting in a range:
	// visit(track, depth, index, counts).
	void ZoneSelfAllocations(const Capture& capture, Range range, const func<void(int64, int64, int64, const ZoneCounts&)>& visit);
	// What one zone counted itself: its counts less its children's.
	ZoneCounts ZoneOwnCounts(const Capture& capture, ZoneRef ref);
	// The allocations each frame of `set` held (indexed like its starts): from allocation events, else the process's
	// count, else the zones'; false when the capture has none of them.
	bool FrameAllocations(const Capture& capture, const FrameSet* set, Core::Array<int64>& objects, Core::Array<int64>& bytes);
	// "events", "zones" or "samples"; false (and `out` untouched) for any other name.
	bool AllocSourceNamed(Core::StringView name, AllocSource& out);
	// What the process counted (AllocCount) between a range's ends.
	void CountedBetween(const Capture& capture, Range range, int64& objects, int64& bytes);
	// Live objects by type over the censuses in a range (HeapCensus), the types that grew most first.
	Core::String CensusTrend(const Capture& capture, int64 fromNs, int64 toNs, int32 limit);

	// What happened inside one zone on its thread: allocations, lock waits, messages and collections.
	void WriteInside(IED::JsonWriter& json, const Capture& capture, ZoneRef ref, int64 begin, int64 end);
	// The frames over a threshold (`thresholdTicks`; twice the median when negative): their indices, longest first.
	void SlowFrames(const Capture& capture, const FrameSet* set, int64 thresholdTicks, Core::Array<int64>& out, int64& threshold, int64& median);
}
