// What every query shares: a time range in the capture's ticks from the nanoseconds a client asks for, numbers written
// as JSON, the names a zone is shown by, and where a zone sits in the capture.

include "capture/Capture.ch"
include "api/Protocol.ch"

namespace Profiler
{
	// [begin, end) in ticks since the capture started.
	struct Range
	{
		public int64 m_Begin;
		public int64 m_End;

		public bool Overlaps(int64 a, int64 b) const { return a < m_End && b > m_Begin; }
		public bool Holds(int64 t) const { return t >= m_Begin && t < m_End; }
		public int64 Length() const { return m_End - m_Begin; }
	}

	// The range a query names in nanoseconds; a bound left out (negative) is the capture's start or its end.
	Range RangeOfNs(const Capture& capture, int64 fromNs, int64 toNs);
	// The whole capture.
	Range Everything(const Capture& capture);

	void WriteNumber(IED::JsonWriter& json, double value, int32 decimals);
	void NumberField(IED::JsonWriter& json, const char* key, double value, int32 decimals);

	// A list of names a JSON answer refers to by index, so a name repeated by thousands of zones is sent once.
	class NameTable
	{
		public int64 Index(Core::StringView name);
		public void Write(IED::JsonWriter& json) const;

		private Core::HashMap<Core::String, int64> m_IndexOf;
		private Core::Array<Core::String> m_Names;
	}

	// The name a track shows: its name at `t`, else "Thread <os id>".
	Core::String TrackLabel(const Capture& capture, const ThreadTrack* track, int64 t);

	// The zone a reference names, if it exists.
	bool ZoneAt(const Capture& capture, ZoneRef ref, const ThreadTrack*& track, const ZoneLevel*& level);

	// The task that was running on `track` at `t`: its name string id, 0 for none.
	uint64 TaskNameAt(const Capture& capture, int32 track, int64 t);

	// The durations, sorted, and the value at a fraction of the way through them (0.5 the median).
	int64 Quantile(const Core::Array<int64>& sorted, double fraction);
}
