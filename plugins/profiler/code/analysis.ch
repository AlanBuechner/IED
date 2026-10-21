// The analysis queries (docs/features/12-profiler.md "Analysis", "Memory"): statistics per zone, one zone's histogram
// and instances, one zone's details, the flame graph, memory and leaks, one frame's breakdown, a capture's summary and
// two captures compared. Each takes the capture (its lock held by the caller) and what was asked, and answers JSON;
// the same answers back the AI's profile_* tools.

include "query.ch"

namespace Profiler
{
	enum class GroupBy { Zone, Thread, Task }
	enum class StatsOrder { Total, Self, Count, Mean, Median, P95, Max, Name }

	struct StatsAsk
	{
		public int64 fromNs = -1;
		public int64 toNs = -1;
		public GroupBy groupBy;
		public StatsOrder order;
		public Core::String filter;      // zones whose name holds this text (any case); empty for all
		public int32 limit = 500;
	}

	struct FindAsk
	{
		public uint64 site;              // a source location id, or 0 to match `name`
		public Core::String name;
		public int64 fromNs = -1;
		public int64 toNs = -1;
		public int32 bins = 64;
		public int64 lowNs = -1;         // instances whose duration is within [lowNs, highNs]
		public int64 highNs = -1;
		public int32 limit = 100;
	}

	struct FlameAsk
	{
		public int64 fromNs = -1;
		public int64 toNs = -1;
		public int32 track = -1;         // one thread, or every one
		public bool samples;             // from call stack samples instead of zones
		public int32 mostNodes = 4000;
	}

	enum class LeakGroup { Callstack, Pool, Thread }

	struct LeakAsk
	{
		public int64 atNs = -1;          // allocations live at this time; the end when left out
		public LeakGroup groupBy;
		public int32 limit = 200;
	}

	Core::String Stats(const Capture& capture, const StatsAsk& ask);
	Core::String FindZone(const Capture& capture, const FindAsk& ask);
	// One zone's details; empty when the reference names no zone.
	Core::String ZoneDetails(const Capture& capture, ZoneRef ref);
	Core::String Flame(const Capture& capture, const FlameAsk& ask);
	Core::String Memory(const Capture& capture, int64 fromNs, int64 toNs, int32 width);
	Core::String Leaks(const Capture& capture, const LeakAsk& ask);
	// One frame of the main frame set: `index`, or the longest with -1. Empty when there is no such frame.
	Core::String FrameBreakdown(const Capture& capture, int64 index);
	Core::String FrameGraph(const Capture& capture, int32 width);
	Core::String Summary(const Capture& capture);
	Core::String Compare(const Capture& a, const Capture& b, int32 limit);
	Core::String Messages(const Capture& capture, int64 fromNs, int64 toNs, int32 leastSeverity, Core::StringView text, int64 offset, int32 limit);

	// A call stack's frames as text, innermost first: a symbol's function (file:line), else module+offset, else the address.
	void WriteCallstack(IED::JsonWriter& json, const Capture& capture, uint64 callstack);
	// The main frame set (the default, else the first); null without frames.
	const FrameSet* MainFrameSet(const Capture& capture);
	bool ContainsNoCase(Core::StringView text, Core::StringView part);
}
