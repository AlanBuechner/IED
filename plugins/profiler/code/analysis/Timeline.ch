// prof.timeline (docs/16 "The server does the heavy lifting"): what a timeline of `width` pixels shows of a range -
// each lane's zones, with runs of zones narrower than a pixel merged into one span that says how many it holds, so
// the answer stays the size of the screen however many zones the range covers. Lanes are threads, or with `tasks`
// the task names (docs/16 "Thread names and the by-task view"); then GPU queues, frames, collections, messages and
// plots in the range.

include "Query.ch"

namespace Profiler
{
	enum class LaneMode { Threads, Tasks }

	struct TimelineAsk
	{
		public int64 m_FromNs = -1;
		public int64 m_ToNs = -1;
		public int32 m_Width = 1000;
		public LaneMode m_Mode;
		public int32 m_MostDepth = 64;
		public int32 m_MostMessages = 300;
	}

	// The answer's JSON (docs/16 "prof.timeline"); the caller holds the capture's lock.
	Core::String Timeline(const Capture& capture, const TimelineAsk& ask);
}
