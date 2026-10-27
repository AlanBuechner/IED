// A thread's zones (docs/16): kept by depth, since zones nest per thread - at one depth they never overlap and arrive
// in time order, so each depth is a sorted run that a range finds by binary search. Times are ticks since the capture
// started. A zone is named by a ZoneRef: its track, its depth and its index at that depth.

include "bedrock.ch"

namespace Profiler
{
	const int64 c_Open = -1;
	const int32 c_MostDepth = 255;

	// The parts of a zone most have none of, kept aside (a zone's `extras` entry is 1 + its index here, 0 for none).
	struct ZoneExtra
	{
		public uint64 m_NameStr;      // ZoneBeginNamed's per-call name
		public uint64 m_TextStr;
		public uint64 m_Value;
		public bool m_HasValue;
		public uint32 m_Color;
		public uint64 m_Callstack;
		public uint64 m_AllocObjects;  // ZoneAllocs: what the zone allocated, its children's included
		public uint64 m_AllocBytes;
		public uint64 m_NativeBytes;   // ZoneNative: the native memory its thread took in it, its children's included
		public uint64 m_NativeFreed;   // ZoneNativeFreed: and gave back in it
		public bool m_Counted;         // a ZoneAllocs, ZoneNative or ZoneNativeFreed came
	}

	// The zones of one depth of one thread, in time order.
	class ZoneLevel
	{
		public Core::Array<int64> m_Starts;
		public Core::Array<int64> m_Ends;      // kOpen while the zone runs
		public Core::Array<int64> m_Selfs;     // the zone's time minus its children's, once it ended
		public Core::Array<uint32> m_Sites;    // source location ids
		public Core::Array<uint32> m_Extras;

		public int64 Count() const { return m_Starts.Count(); }
		// The first zone that ends after `t` (or is still open): where a range starting at `t` begins.
		public int64 FirstEndingAfter(int64 t) const;
		// The first zone that starts at or after `t`.
		public int64 FirstStartingAt(int64 t) const;
		// The zone's end, or `now` for an open one.
		public int64 EndOr(int64 i, int64 now) const { return m_Ends[i] == c_Open ? now : m_Ends[i]; }
	}

	// A zone's address: track index, depth and index at that depth, packed (16, 8 and 40 bits).
	struct ZoneRef
	{
		public uint64 m_Bits;

		public static ZoneRef Of(int64 track, int64 depth, int64 index);
		public int64 Track() const { return (int64)(m_Bits >> 48); }
		public int64 Depth() const { return (int64)((m_Bits >> 40) & 0xFF); }
		public int64 Index() const { return (int64)(m_Bits & 0xFFFFFFFFFF); }
		// "track:depth:index", how the protocol names a zone.
		public Core::String Text() const;
		public static bool Parse(Core::StringView text, ZoneRef& out);
	}

	// A zone being recorded: where it is and how much of its time its children took so far.
	struct OpenZone
	{
		public int32 m_Depth;
		public int64 m_Index;
		public int64 m_ChildTicks;
	}
}
